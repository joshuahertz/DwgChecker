#include "MainWindow.h"

#include "AutoCadController.h"
#include "Canonicalizer.h"
#include "Csv.h"
#include "DetailDialog.h"
#include "DuplicatesTab.h"
#include "PathCard.h"
#include "Platform.h"
#include "PreviewDialog.h"
#include "ResultsModel.h"
#include "SettingsPanel.h"
#include "ViewEditorDialog.h"

#include <QApplication>
#include <QCloseEvent>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonDocument>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSaveFile>
#include <QSettings>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QStackedWidget>
#include <QStyle>
#include <QTimer>
#include <QTabWidget>
#include <QTreeView>
#include <QInputDialog>
#include <QSet>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <exception>
#include <functional>

using namespace dc;

namespace dcgui {

namespace {

// The reading rules the solution drawings are read with (layers, paper space, ..., and what
// referenceCanon() takes from the matching rules: tolerances, merging by layer or color), as
// text, to notice when they change.
QByteArray canonSignature(const AppSettings& s) {
    QJsonObject o = s.toJson().value(QStringLiteral("canon")).toObject();
    const CanonSettings c = s.referenceCanon();
    o.insert(QStringLiteral("linTol"), c.linTol);
    o.insert(QStringLiteral("angTol"), c.angTol);
    o.insert(QStringLiteral("mergeByLayer"), c.mergeByLayer);
    o.insert(QStringLiteral("mergeByColor"), c.mergeByColor);
    o.insert(QStringLiteral("colorTolerance"), c.colorTolerance);
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

const char* const kRecentRefs = "recent/references";
const char* const kRecentDirs = "recent/folders";
constexpr int kMaxRecent = 8;
}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    m_acad = new AutoCadController(this);
    buildUi();
    loadState();
    probeParser();
    connect(&m_watcher, &QFutureWatcher<BatchOutput>::finished, this, &MainWindow::onFinished);
}

MainWindow::~MainWindow() {
    m_cancel = true;
    m_watcher.waitForFinished();
}

void MainWindow::buildUi() {
    setWindowTitle(tr("DWG Checker"));
    resize(1400, 900);
    auto* central = new QWidget(this);
    auto* outer = new QHBoxLayout(central);
    m_split = new QSplitter(Qt::Horizontal, central);
    m_split->setChildrenCollapsible(false);
    outer->addWidget(m_split);

    // ---- Left column, top to bottom: the reference, the submissions, Run; then Settings ----
    auto* left = new QWidget(m_split);
    left->setMinimumWidth(340);
    auto* lay = new QVBoxLayout(left);
    lay->setContentsMargins(0, 0, 6, 0);
    m_refCard = new PathCard(PathCard::Kind::File, 1, tr("Reference (solution)"), tr("Click to choose the reference drawing"),
                             tr("Drawings (*.dwg *.json)"), {QStringLiteral("dwg"), QStringLiteral("json")}, true, left);
    // One solution drawing: shown from the card (several: from their names in step 2).
    m_showRef = new QToolButton(m_refCard);
    m_showRef->setText(tr("Show drawing"));
    m_showRef->setToolTip(tr("Show the solution drawing in its own colors and linetypes"));
    m_showRef->setVisible(false);
    m_refCard->addButton(m_showRef);
    connect(m_showRef, &QToolButton::clicked, this, [this]() {
        if (m_refCard->paths().size() == 1) previewDrawing(m_refCard->path());
    });
    m_dirCard = new PathCard(PathCard::Kind::Folder, 3, tr("Student submissions"), tr("Click to choose the submissions folder"),
                             QString(), {}, false, left);
    // Step 2 (built by the settings panel, which keeps it in step with the other settings):
    // the location mode, chosen for every assignment.
    m_settings = new SettingsPanel(m_acad, left);
    m_modeBox = new StepBox(2, m_settings->modeStepWidget(), left);
    lay->addWidget(m_refCard);
    lay->addWidget(m_modeBox);
    lay->addWidget(m_dirCard);
    m_runBtn = new QPushButton(tr("Run"), left);
    m_runBtn->setDefault(true);
    m_runBtn->setIconSize(QSize(24, 24));  // the step-3 badge (updateSteps)
    QFont runFont = m_runBtn->font();
    runFont.setBold(true);
    runFont.setPointSizeF(runFont.pointSizeF() * 1.2);
    m_runBtn->setFont(runFont);
    m_runBtn->setMinimumHeight(48);
    m_runBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_cancelBtn = new QPushButton(tr("Cancel"), left);
    m_cancelBtn->setMinimumHeight(48);
    m_cancelBtn->setVisible(false);  // shown while a batch runs
    auto* runRow = new QHBoxLayout;
    runRow->addWidget(m_runBtn, 1);
    runRow->addWidget(m_cancelBtn);
    lay->addLayout(runRow);
    m_status = new QLabel(left);
    m_status->setWordWrap(true);
    m_progress = new QProgressBar(left);
    m_progress->setVisible(false);
    lay->addWidget(m_status);
    lay->addWidget(m_progress);

    // Settings fill the rest of the column and scroll; folded away, the space stays empty.
    m_settingsToggle = new QToolButton(left);
    m_settingsToggle->setText(tr("Settings"));
    m_settingsToggle->setCheckable(true);
    m_settingsToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_settingsToggle->setArrowType(Qt::RightArrow);
    m_settingsToggle->setAutoRaise(true);
    lay->addSpacing(4);
    lay->addWidget(m_settingsToggle);
    m_settingsArea = new QScrollArea(left);
    m_settingsArea->setWidget(m_settings);
    m_settingsArea->setWidgetResizable(true);
    m_settingsArea->setFrameShape(QFrame::NoFrame);
    m_settingsArea->setVisible(false);
    lay->addWidget(m_settingsArea, 1);
    lay->addStretch();  // takes the space while Settings is folded

    // ---- Right: the tabs ----
    m_tabs = new QTabWidget(m_split);
    auto* resultsPage = new QWidget(m_tabs);
    auto* rl = new QVBoxLayout(resultsPage);
    m_table = new QTreeView(resultsPage);
    m_model = new ResultsModel(this);
    m_proxy = new QSortFilterProxyModel(this);
    m_proxy->setSourceModel(m_model);
    m_proxy->setSortRole(SortRole);
    m_table->setModel(m_proxy);
    m_delegate = new ResultsDelegate(m_table);
    m_table->setItemDelegate(m_delegate);
    m_table->setSortingEnabled(true);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setWordWrap(true);
    m_table->setMouseTracking(true);
    m_table->setUniformRowHeights(false);
    m_table->setTreePosition(ColName);  // the fold arrow sits in the Student column
    m_table->setIndentation(18);
    m_table->setAllColumnsShowFocus(true);
    m_table->setExpandsOnDoubleClick(false);
    m_table->header()->setStretchLastSection(false);
    m_table->header()->setSectionResizeMode(ColFlag, QHeaderView::ResizeToContents);
    m_table->header()->setSectionResizeMode(ColErrors, QHeaderView::Stretch);
    m_table->setColumnWidth(ColName, 260);
    m_table->setColumnWidth(ColCorrectness, 110);
    m_table->setColumnWidth(ColAcad, 130);
    m_table->sortByColumn(ColName, Qt::AscendingOrder);
    // Subtly alternating rows: the base color moved 4.5% toward the text color (light or dark theme).
    m_table->setAlternatingRowColors(true);
    {
        QPalette pal = m_table->palette();
        pal.setColor(QPalette::AlternateBase, blendColors(pal.color(QPalette::Base), pal.color(QPalette::Text), 0.045));
        m_table->setPalette(pal);
    }
    m_resultsStack = new QStackedWidget(resultsPage);
    m_guide = new QLabel(m_resultsStack);
    m_guide->setAlignment(Qt::AlignCenter);
    m_guide->setTextFormat(Qt::RichText);
    m_guide->setWordWrap(true);
    m_guide->setContentsMargins(24, 0, 24, 0);
    QFont guideFont = m_guide->font();
    guideFont.setPointSizeF(guideFont.pointSizeF() * 1.25);
    m_guide->setFont(guideFont);
    m_resultsStack->addWidget(m_guide);  // 0: what to do next
    m_resultsStack->addWidget(m_table);  // 1: results
    rl->addWidget(m_resultsStack);
    auto* exportRow = new QHBoxLayout;
    auto* exportBtn = new QPushButton(tr("Export results CSV…"), resultsPage);
    auto* tableHint = new QLabel(tr("Click a row to see its errors in the detail view (a student with several files "
                                    "folds open); click \"Open in AutoCAD\" to open the file there."),
                                 resultsPage);
    tableHint->setForegroundRole(QPalette::PlaceholderText);
    tableHint->setWordWrap(true);
    exportRow->addWidget(tableHint, 1);
    exportRow->addWidget(exportBtn);
    rl->addLayout(exportRow);
    m_tabs->addTab(resultsPage, tr("Results"));

    auto* dupPage = new QWidget(m_tabs);
    auto* dl = new QVBoxLayout(dupPage);
    m_dups = new DuplicatesTab(dupPage);
    dl->addWidget(m_dups);
    auto* dupExportRow = new QHBoxLayout;
    auto* dupExport = new QPushButton(tr("Export possible cheating CSV…"), dupPage);
    dupExportRow->addStretch();
    dupExportRow->addWidget(dupExport);
    dl->addLayout(dupExportRow);
    m_tabs->addTab(dupPage, tr("Possible cheating"));

    m_split->addWidget(left);
    m_split->addWidget(m_tabs);
    m_split->setStretchFactor(0, 0);  // a wider window widens the results
    m_split->setStretchFactor(1, 1);
    m_split->setSizes({470, 930});

    // The log lives in its own window (Help > Show log).
    m_logWindow = new QDialog(this, Qt::Window);
    m_logWindow->setWindowTitle(tr("DWG Checker log"));
    m_logWindow->resize(820, 420);
    auto* logLay = new QVBoxLayout(m_logWindow);
    m_log = new QPlainTextEdit(m_logWindow);
    m_log->setReadOnly(true);
    logLay->addWidget(m_log);
    auto* logButtons = new QDialogButtonBox(QDialogButtonBox::Close, m_logWindow);
    connect(logButtons, &QDialogButtonBox::rejected, m_logWindow, &QDialog::hide);
    logLay->addWidget(logButtons);
    setCentralWidget(central);

    // ---- Menu ----
    QMenu* file = menuBar()->addMenu(tr("&File"));
    file->addAction(tr("Export results CSV…"), this, &MainWindow::exportResults);
    file->addAction(tr("Export possible cheating CSV…"), this, &MainWindow::exportDuplicates);
    file->addSeparator();
    file->addAction(tr("Save settings as JSON…"), this, [this]() {
        const QString p = QFileDialog::getSaveFileName(this, tr("Save settings"), QString(), tr("JSON (*.json)"));
        if (p.isEmpty()) return;
        // Written whole or not at all, and a failure is said (D76).
        QSaveFile f(p);
        if (!f.open(QIODevice::WriteOnly) || f.write(currentSettings().toJsonText(false)) < 0 || !f.commit())
            QMessageBox::warning(this, tr("Save settings"),
                                 tr("Could not save %1: %2").arg(QDir::toNativeSeparators(p), f.errorString()));
    });
    file->addAction(tr("Load settings from JSON…"), this, [this]() {
        const QString p = QFileDialog::getOpenFileName(this, tr("Load settings"), QString(), tr("JSON (*.json)"));
        if (p.isEmpty()) return;
        QFile f(p);
        if (!f.open(QIODevice::ReadOnly)) {
            QMessageBox::warning(this, tr("Load settings"), tr("Could not open %1: %2").arg(QDir::toNativeSeparators(p), f.errorString()));
            return;
        }
        // A file that is not settings changes nothing (it used to reset everything; D76).
        QString why;
        const std::optional<AppSettings> s = AppSettings::fromJsonText(f.readAll(), &why);
        if (!s) {
            QMessageBox::warning(this, tr("Load settings"),
                                 tr("%1 was not loaded: %2. The settings are unchanged.").arg(QFileInfo(p).fileName(), why));
            return;
        }
        applySettings(*s);
        storeQuizSettings();
    });
    file->addSeparator();
    file->addAction(tr("Quit"), this, &QWidget::close);
    QMenu* help = menuBar()->addMenu(tr("&Help"));
    help->addAction(tr("Show log"), QKeySequence(Qt::CTRL | Qt::Key_L), this, [this]() {
        m_logWindow->show();
        m_logWindow->raise();
        m_logWindow->activateWindow();
    });
    help->addSeparator();
    help->addAction(tr("About"), this, [this]() {
        // The licenses folder of an installed package (see packaging/licenses); a build folder has none.
#ifdef Q_OS_MACOS
        const QString licenses = QDir(QCoreApplication::applicationDirPath() + QStringLiteral("/../Resources/licenses")).absolutePath();
#else
        const QString licenses = QCoreApplication::applicationDirPath() + QStringLiteral("/licenses");
#endif
        const QString where = QFileInfo(licenses).isDir()
            ? tr("<a href=\"%1\">the licenses folder</a> that comes with the app")
                  .arg(QUrl::fromLocalFile(licenses).toString(QUrl::FullyEncoded).toHtmlEscaped())
            : tr("the licenses folder of an installed copy");
        QMessageBox::about(this, tr("About DWG Checker"),
                           tr("<b>DWG Checker %1</b><br>Grades CAD assignments against a reference drawing and shows evidence of "
                              "shared files.<br><br>DWG Checker was designed by Joshua Hertz [j.hertz@northeastern.edu]; "
                              "its code was written with Claude (Anthropic).<br><br>"
                              "Parser: %2<br>LibreDWG pinned version: %3<br>Qt %4<br><br>"
                              "Free software under the GNU General Public License version 3, with no warranty. It uses Qt "
                              "(GNU LGPL version 3) and dwgread from GNU LibreDWG (GNU GPL version 3 or later). Their "
                              "licenses and where to get the source code are in %5.")
                               .arg(QStringLiteral(DWGCHECK_VERSION),
                                    m_runLoader ? m_runLoader->parserVersion().toHtmlEscaped() : tr("see Log"),
                                    QStringLiteral(DWGCHECK_LIBREDWG_VERSION), QString::fromLatin1(qVersion()), where));
    });
    help->addAction(tr("About Qt"), qApp, &QApplication::aboutQt);

    // ---- Wiring ----
    connect(m_refCard, &PathCard::pathsChanged, this, &MainWindow::referenceChanged);
    connect(m_dirCard, &PathCard::pathsChanged, this, &MainWindow::folderChanged);
    connect(m_refCard, &PathCard::clearRecentRequested, this, [this]() { clearRecent(kRecentRefs, m_refCard); });
    connect(m_dirCard, &PathCard::clearRecentRequested, this, [this]() { clearRecent(kRecentDirs, m_dirCard); });
    connect(m_settingsToggle, &QToolButton::toggled, this, [this](bool on) {
        m_settingsArea->setVisible(on);
        m_settingsToggle->setArrowType(on ? Qt::DownArrow : Qt::RightArrow);
        QSettings().setValue("ui/settingsOpen", on);
    });
    connect(m_settings, &SettingsPanel::changed, this, &MainWindow::storeQuizSettings);
    connect(m_settings, &SettingsPanel::changed, this, [this]() {  // the solution card's counts follow the reading rules
        if (canonSignature(currentSettings()) != m_refCanonSig) updateRefInfo();
    });
    connect(m_settings, &SettingsPanel::editViewsRequested, this, &MainWindow::editViews);
    connect(m_settings, &SettingsPanel::previewRequested, this, &MainWindow::previewDrawing);
    connect(m_runBtn, &QPushButton::clicked, this, &MainWindow::run);
    connect(m_cancelBtn, &QPushButton::clicked, this, &MainWindow::cancel);
    connect(exportBtn, &QPushButton::clicked, this, &MainWindow::exportResults);
    connect(dupExport, &QPushButton::clicked, this, &MainWindow::exportDuplicates);
    connect(m_table, &QTreeView::clicked, this, [this](const QModelIndex& idx) {
        const QModelIndex src = m_proxy->mapToSource(idx);
        const int file = src.data(StudentIndexRole).toInt();
        if (idx.column() == ColAcad && file >= 0) {
            openInAutoCad(file);
        } else if (src.data(HasChildrenRole).toBool()) {  // a student with several files: fold
            const QModelIndex first = idx.siblingAtColumn(0);
            m_table->setExpanded(first, !m_table->isExpanded(first));
        } else if (file >= 0) {
            openDetail(file);
        }
    });
    m_table->viewport()->setCursor(Qt::PointingHandCursor);  // every cell opens something
    // The rows' heights follow the column widths: laid out again once a drag pauses, not for
    // every pixel of it (D76).
    m_relayout = new QTimer(this);
    m_relayout->setSingleShot(true);
    m_relayout->setInterval(60);
    connect(m_relayout, &QTimer::timeout, m_table, [this]() { m_table->doItemsLayout(); });
    connect(m_table->header(), &QHeaderView::sectionResized, m_relayout, qOverload<>(&QTimer::start));
}

AppSettings MainWindow::currentSettings() const { return m_settings->settings(); }

void MainWindow::applySettings(const AppSettings& s) {
    m_settings->setSettings(s);  // signals blocked: the card's reading is checked here
    if (!m_refCard->paths().isEmpty() && canonSignature(currentSettings()) != m_refCanonSig) updateRefInfo();
}

void MainWindow::closePreviews() {
    for (const QPointer<QDialog>& d : std::as_const(m_previews))
        if (d) d->close();
    m_previews.clear();
}

void MainWindow::loadState() {
    QSettings q;
    m_settings->setSettings(AppSettings::fromJsonText(q.value("settings/json").toByteArray()).value_or(AppSettings{}));
    m_refCard->setRecent(q.value(kRecentRefs).toStringList());
    m_dirCard->setRecent(q.value(kRecentDirs).toStringList());
    // The app opens with nothing chosen: step 1 is next. Recent choices are one click away.
    referenceChanged();
    folderChanged();
    restoreGeometry(q.value("window/geometry").toByteArray());
    m_split->restoreState(q.value("window/splitter").toByteArray());
    m_settingsToggle->setChecked(q.value("ui/settingsOpen", true).toBool());  // open unless folded last time
}

void MainWindow::saveState() const {
    QSettings q;
    q.setValue("settings/json", currentSettings().toJsonText());
    q.setValue("window/geometry", saveGeometry());
    q.setValue("window/splitter", m_split->saveState());
}

void MainWindow::closeEvent(QCloseEvent* e) {
    saveState();
    QMainWindow::closeEvent(e);
}

void MainWindow::log(const QString& line) {
    m_log->appendPlainText(QStringLiteral("[%1] %2").arg(QDateTime::currentDateTime().toString("HH:mm:ss"), line));
}

namespace {

QString countText(int n, const QString& one, const QString& many) { return QStringLiteral("%1 %2").arg(n).arg(n == 1 ? one : many); }

// Settings are remembered per assignment: the set of solution drawings, by full path (the
// registry key keeps its old name, "quizSettings", so saved settings still load).
QString quizKey(const QStringList& refPaths) {
    QStringList ps;
    for (const QString& r : refPaths) {
        QString p = QDir::cleanPath(QFileInfo(r).absoluteFilePath());
#ifdef Q_OS_WIN
        p = p.toLower();
#endif
        ps << p;
    }
    ps.sort();
    return QStringLiteral("quizSettings/") +
           QString::fromLatin1(QCryptographicHash::hash(ps.join(QLatin1Char('|')).toUtf8(), QCryptographicHash::Sha1).toHex());
}

// What the solution card says, and what each drawing contains (for Settings).
struct RefSummary {
    bool ok = false;
    QString text;
    std::map<QString, DrawingContent> contents;  // by AppSettings::drawingKey
    // By path, each drawing read (also those before one that failed), for the preview and
    // Edit views, and its own line ("inches  ·  42 objects, 2 dimensions") for the preview.
    std::map<QString, DrawingPtr> drawings;
    std::map<QString, QString> summaries;
};

// "inches  ·  42 objects, 2 dimensions; ignoring 51 objects on turned-off or frozen layers",
// or "inches" and then a line per drawing ("Part A – 42 objects, …") when there are several.
// Read in the background; `progress(i, n, name)` is called before reading drawing i of n.
// A newer read sets `cancel`: this one then stops before its next drawing (D76).
RefSummary describeReferences(const QStringList& paths, const AppSettings& s,
                              const std::function<void(int, int, const QString&)>& progress, const std::atomic_bool& cancel) {
    AutoLoader loader(s.loader);
    RefSummary out;
    QStringList lines, units;
    for (const QString& path : paths) {
        if (cancel) return out;  // superseded: nobody reads the result
        progress(static_cast<int>(lines.size()), static_cast<int>(paths.size()), QFileInfo(path).completeBaseName());
        QString err;
        const auto d = loadDrawing(path, loader, s.referenceCanon(), &err);
        if (!d) {
            out.text = QObject::tr("could not read %1: %2").arg(QFileInfo(path).fileName(), err);
            return out;
        }
        int objects = 0, dims = 0, hatches = 0;
        for (const auto& p : d->prims) {
            objects += isGeometric(p.kind);
            dims += p.kind == PrimKind::Dimension;
            hatches += p.kind == PrimKind::Hatch;
        }
        out.contents[AppSettings::drawingKey(path)] = DrawingContent{dims > 0, hatches > 0};
        QStringList counts{countText(objects, QObject::tr("object"), QObject::tr("objects"))};
        if (dims > 0) counts << countText(dims, QObject::tr("dimension"), QObject::tr("dimensions"));
        if (hatches > 0) counts << countText(hatches, QObject::tr("hatch"), QObject::tr("hatches"));
        QString line = counts.join(QStringLiteral(", "));
        if (d->hiddenLayerObjects > 0)  // e.g. a sketching grid the instructor turned off
            line += (d->hiddenLayerObjectsSkipped ? QObject::tr("; ignoring %1 on turned-off or frozen layers")
                                                  : QObject::tr("; including %1 on turned-off or frozen layers"))
                        .arg(countText(d->hiddenLayerObjects, QObject::tr("object"), QObject::tr("objects")));
        out.drawings[path] = d;
        out.summaries[path] = d->meta.insUnits > 0 ? insUnitsName(d->meta.insUnits) + QStringLiteral("  ·  ") + line : line;
        if (d->meta.insUnits > 0 && !units.contains(insUnitsName(d->meta.insUnits))) units << insUnitsName(d->meta.insUnits);
        lines << (paths.size() > 1 ? QStringLiteral("%1 \u2013 %2").arg(QFileInfo(path).completeBaseName(), line) : line);
    }
    const QString sep = paths.size() > 1 ? QStringLiteral("\n") : QStringLiteral("  ·  ");  // one drawing per line
    out.text = lines.join(sep);
    if (!units.isEmpty()) out.text = units.join(QStringLiteral(" / ")) + sep + out.text;
    out.ok = true;
    return out;
}

// What the submissions card says ("33 drawings from 30 students"). Scanned in the background,
// so that a large folder, or one on a streaming drive, never holds up the window (D76).
struct DirSummary {
    int files = 0;
    QString text;
    bool problem = false;
};

// The submissions in a folder: a solution drawing kept in the same folder is not one.
QStringList submissionFiles(const QString& dir, const QStringList& refs) {
    QStringList files = findSubmissionFiles(dir);
    files.removeIf([&](const QString& f) {
        return std::any_of(refs.cbegin(), refs.cend(), [&](const QString& r) { return samePath(f, r); });
    });
    return files;
}

// The card's line for those files; no I/O, so Run can describe the files it just found
// rather than scan the folder twice (D76).
DirSummary describeFiles(const QStringList& files, const QString& identityRegex) {
    DirSummary out;
    const int n = static_cast<int>(files.size());
    out.files = n;
    // Files are grouped by student as in a run (name + Canvas user id).
    const IdentityParser idp(identityRegex);
    QSet<QString> students;
    for (const QString& f : files) students.insert(studentKey(idp.parse(f)));
    const bool json = n > 0 && files.front().endsWith(QStringLiteral(".json"), Qt::CaseInsensitive);
    const QString what = json ? countText(n, QObject::tr("JSON dump"), QObject::tr("JSON dumps"))
                              : countText(n, QObject::tr("drawing"), QObject::tr("drawings"));
    if (n == 0) {
        out.text = QObject::tr("no drawings in this folder");
        out.problem = true;
    } else if (students.size() < n) {
        out.text = QObject::tr("%1 from %2").arg(what, countText(static_cast<int>(students.size()), QObject::tr("student"),
                                                                 QObject::tr("students")));
    } else {
        out.text = QObject::tr("%1 found").arg(what);
    }
    return out;
}

DirSummary describeFolder(const QString& dir, const QStringList& refs, const QString& identityRegex) {
    return describeFiles(submissionFiles(dir, refs), identityRegex);
}

// What an exception in a background task says; it is caught there, since QFuture::result()
// would throw it again in the window's slot and end the app (D76).
QString exceptionText(std::exception_ptr e) {
    try {
        std::rethrow_exception(e);
    } catch (const std::exception& x) {
        return QString::fromLocal8Bit(x.what());
    } catch (...) {
        return QObject::tr("unknown error");
    }
}

}  // namespace

void MainWindow::referenceChanged() {
    QElapsedTimer busy;  // the window's own work here, for the log (the reading is in the background)
    busy.start();
    const QStringList paths = m_refCard->paths();
    addRecent(kRecentRefs, m_refCard, PathCard::encode(paths));
    // Previews of the previous choice ("Drawing 2" may now be another file) close.
    m_pendingPreview.clear();
    m_refDrawings.clear();
    m_refSummaries.clear();
    closePreviews();
    m_showRef->setVisible(paths.size() == 1);
    m_refReadable = 0;
    m_settings->setSolutionDrawings(paths);  // several: each gets its own mode and checks
    if (paths.isEmpty()) {
        endRefRead();
        m_refCard->setCaptionNote(QString());
        m_refCard->setDetail(tr("The solution drawing the submissions are graded against; choose several for a multi-drawing "
                                "assignment (or drop them here)"));
        updateDirInfo();
        return;
    }
    const std::optional<AppSettings> saved = AppSettings::fromJsonText(QSettings().value(quizKey(paths)).toByteArray());
    if (saved) {
        m_settings->setSettings(*saved);  // the drawings are read below with these rules
        m_refCard->setCaptionNote(paths.size() > 1 ? tr("settings saved for these reference solutions")
                                                   : tr("settings saved for this reference solution"));
        log(tr("loaded the settings saved for %1").arg(m_refCard->paths().size() > 1 ? tr("this set of drawings")
                                                                                       : QFileInfo(paths.front()).fileName()));
    } else {
        m_refCard->setCaptionNote(paths.size() > 1 ? tr("new reference solutions: check the settings")
                                                   : tr("new reference solution: check the settings"));
    }
    updateRefInfo();
    updateDirInfo();
    log(tr("chose %1: %2 ms in the window, then reading in the background")
            .arg(countText(static_cast<int>(paths.size()), tr("solution drawing"), tr("solution drawings")))
            .arg(busy.elapsed()));
}

void MainWindow::folderChanged() {
    addRecent(kRecentDirs, m_dirCard, m_dirCard->path());
    updateDirInfo();
}

void MainWindow::updateRefInfo() {
    const QStringList paths = m_refCard->paths();
    if (paths.isEmpty()) return;
    for (const QString& p : paths)
        if (!QFileInfo(p).isFile()) {
            endRefRead();
            m_refReadable = -1;
            m_refCard->setDetail(tr("file not found: %1").arg(QDir::toNativeSeparators(p)), true);
            updateSteps();
            return;
        }
    // Reading takes a few seconds per drawing (dwgread): a progress bar on the card, which
    // moves for one drawing and counts several, and the busy cursor (arrow and spinner; the
    // window stays usable). It is the application's cursor: one set on the window doesn't show
    // over the card, which has its own pointing hand, and the mouse is usually there.
    const int read = ++m_refRead;
    m_refReading = true;
    if (m_refCancel) *m_refCancel = true;  // an earlier read still running stops at its next drawing
    m_refCancel = std::make_shared<std::atomic_bool>(false);
    m_refDrawings.clear();  // read again with the current reading rules
    m_refSummaries.clear();
    closePreviews();  // they show the drawings as read with the old rules (D76)
    m_refCard->setDetail(paths.size() > 1 ? tr("reading %1 drawings…").arg(paths.size()) : tr("reading…"));
    m_refCard->showProgress(0, static_cast<int>(paths.size()));
    if (!m_busyCursor) {
        QApplication::setOverrideCursor(Qt::BusyCursor);
        m_busyCursor = true;
    }
    m_refCard->repaint();  // shown now, even if the window is kept busy a moment longer
    QElapsedTimer readTime;
    readTime.start();
    AppSettings s = currentSettings();
    m_refCanonSig = canonSignature(s);
    s.loader.dwgreadPath = platform::findDwgRead();
    QPointer<MainWindow> self(this);
    auto progress = [self, read](int done, int total, const QString& name) {
        if (total < 2) return;  // the moving bar already says it
        QMetaObject::invokeMethod(
            qApp, [self, read, done, total, name]() {
                if (!self || read != self->m_refRead) return;  // a newer read, or the choice was cleared
                self->m_refCard->showProgress(done, total);
                self->m_refCard->setDetail(
                    tr("reading %1 (%2 of %3)…").arg(name, QString::number(done + 1), QString::number(total)));
            },
            Qt::QueuedConnection);
    };
    auto* w = new QFutureWatcher<RefSummary>(this);
    connect(w, &QFutureWatcherBase::finished, this, [this, w, read, readTime, n = paths.size()]() {
        const RefSummary res = w->result();
        w->deleteLater();
        if (read != m_refRead) return;  // a newer choice is being read, or the choice was cleared
        log(tr("read %1 in %2 s").arg(countText(static_cast<int>(n), tr("solution drawing"), tr("solution drawings")))
                .arg(readTime.elapsed() / 1000.0, 0, 'f', 1));
        const QString pending = m_pendingPreview;  // a drawing clicked while reading
        endRefRead();
        m_refReadable = res.ok ? 1 : -1;
        m_refDrawings = res.drawings;
        m_refSummaries = res.summaries;
        m_refCard->setDetail(res.text, !res.ok);
        if (res.ok) m_settings->setDrawingContents(res.contents);  // checks only for what the drawings have
        updateSteps();
        if (!pending.isEmpty()) previewDrawing(pending);
    });
    w->setFuture(QtConcurrent::run([paths, s, progress, cancel = m_refCancel]() {
        try {
            return describeReferences(paths, s, progress, *cancel);
        } catch (...) {
            RefSummary r;
            r.text = QObject::tr("could not read the drawings: %1").arg(exceptionText(std::current_exception()));
            return r;
        }
    }));
}

void MainWindow::endRefRead() {
    ++m_refRead;  // a read still running no longer updates the card...
    if (m_refCancel) *m_refCancel = true;  // ...and stops before its next drawing
    m_refReading = false;
    m_pendingPreview.clear();
    m_refCard->hideProgress();
    if (m_busyCursor) {
        QApplication::restoreOverrideCursor();
        m_busyCursor = false;
    }
}

void MainWindow::previewDrawing(const QString& refPath) {
    if (const QPointer<QDialog> open = m_previews.value(refPath)) {
        open->raise();
        open->activateWindow();
        return;
    }
    const QString name = QFileInfo(refPath).fileName();
    const auto it = m_refDrawings.find(refPath);
    if (it == m_refDrawings.end()) {
        if (m_refReading) {  // the card shows the progress; the preview opens when it ends
            m_pendingPreview = refPath;
            return;
        }
        QMessageBox::warning(this, tr("Show drawing"),
                             tr("%1 could not be read; the reference card says why.").arg(name));
        return;
    }
    // "Drawing 2", as the results call it (the card's order).
    const QStringList refs = m_refCard->paths();
    const int k = static_cast<int>(refs.indexOf(refPath));
    const QString title = refs.size() > 1 && k >= 0 ? tr("Drawing %1: %2").arg(QString::number(k + 1), name) : name;
    auto* dlg = new DrawingPreviewDialog(it->second, title, m_refSummaries[refPath], this);
    m_previews.insert(refPath, dlg);
    dlg->show();
}

void MainWindow::updateDirInfo() {
    const QString dir = m_dirCard->path();
    const int scan = ++m_dirScan;  // a scan still running no longer updates the card
    if (dir.isEmpty()) {
        m_dirCount = 0;
        m_dirScanned.clear();
        m_dirCard->setDetail(tr("The folder with the students' drawings; a Canvas download works as-is (or drop it here)"));
        updateSteps();
        return;
    }
    if (!QFileInfo(dir).isDir()) {
        m_dirCount = 0;
        m_dirScanned.clear();
        m_dirCard->setDetail(tr("folder not found"), true);
        updateSteps();
        return;
    }
    // Scanned in the background (D76). The same folder keeps its count meanwhile; a new one
    // waits for its own.
    if (!samePath(dir, m_dirScanned)) {
        m_dirCount = 0;
        m_dirCard->setDetail(tr("looking for drawings…"));
        updateSteps();
    }
    auto* w = new QFutureWatcher<DirSummary>(this);
    connect(w, &QFutureWatcherBase::finished, this, [this, w, scan, dir]() {
        const DirSummary res = w->result();
        w->deleteLater();
        if (scan != m_dirScan) return;
        m_dirScanned = dir;
        m_dirCount = res.files;
        m_dirCard->setDetail(res.text, res.problem);
        updateSteps();
    });
    w->setFuture(QtConcurrent::run([dir, refs = m_refCard->paths(), regex = currentSettings().identityRegex]() {
        try {
            return describeFolder(dir, refs, regex);
        } catch (...) {
            DirSummary r;
            r.text = QObject::tr("could not read the folder: %1").arg(exceptionText(std::current_exception()));
            r.problem = true;
            return r;
        }
    }));
}

void MainWindow::updateSteps() {
    using State = PathCard::State;
    const bool running = m_watcher.isRunning();
    const bool refDone = !m_refCard->path().isEmpty() && m_refReadable >= 0;  // still reading counts as chosen
    // Step 2 always has a location mode (the default, or the one saved for these drawings),
    // which may well be right as it is: it never holds up Run (D68).
    const bool modeDone = refDone;
    const bool dirDone = m_dirCount > 0;
    const bool ready = refDone && dirDone;
    m_refCard->setState(refDone ? State::Done : State::Active);
    m_modeBox->setState(modeDone ? State::Done : State::Waiting);
    m_dirCard->setState(dirDone ? State::Done : refDone ? State::Active : State::Waiting);
    m_runBtn->setEnabled(ready && !running);
    m_runBtn->setIcon(QIcon(stepBadge(4, ready ? State::Active : State::Waiting, 24, devicePixelRatioF())));
    m_runBtn->setToolTip(ready     ? tr("Step 4: grade the %1 drawings").arg(m_dirCount)
                         : !refDone ? tr("First choose the reference drawing (step 1)")
                                    : tr("Next choose the submissions folder (step 3)"));

    // The guide shown in Results until the first run: done steps checked, the next one in bold.
    const int next = !refDone ? 1 : !dirDone ? 3 : 4;
    auto line = [next](int n, bool done, const QString& text) {
        QString t = QStringLiteral("%1&nbsp;&nbsp;%2").arg(done ? QStringLiteral("&#10003;") : QString::number(n), text);
        if (n == next) t = QStringLiteral("<b>%1</b>").arg(t);
        else if (done) t = QStringLiteral("<span style='color:gray'>%1</span>").arg(t);
        return QStringLiteral("<p>%1</p>").arg(t);
    };
    m_guide->setText(QStringLiteral("<h2>%1</h2>").arg(tr("Grade a set of drawings in four steps")) +
                     line(1, refDone, tr("Choose the reference (solution) drawing, or several for a multi-drawing assignment: "
                                         "click the first card on the left.")) +
                     line(2, modeDone, tr("Check the location mode (with several drawings, one for each) in the second box. "
                                          "It is remembered for these reference solutions.")) +
                     line(3, dirDone, tr("Choose the folder of student submissions: click the third card.")) +
                     line(4, false, tr("Click <b>Run</b>. The results appear here; click a row for its details.")));
}

void MainWindow::storeQuizSettings() {
    const QStringList refs = m_refCard->paths();
    if (refs.isEmpty()) return;
    QSettings().setValue(quizKey(refs), currentSettings().toJsonText());
    m_refCard->setCaptionNote(refs.size() > 1 ? tr("settings saved for these reference solutions")
                                              : tr("settings saved for this reference solution"));
}

void MainWindow::addRecent(const char* key, PathCard* card, const QString& path) {
    if (path.isEmpty()) return;
    QSettings q;
    QStringList l = q.value(key).toStringList();
    l.removeAll(path);
    l.prepend(path);
    while (l.size() > kMaxRecent) l.removeLast();
    q.setValue(key, l);
    card->setRecent(l);
}

void MainWindow::clearRecent(const char* key, PathCard* card) {
    QSettings().remove(key);
    card->setRecent({});
}

void MainWindow::probeParser() {
    // Log the dwgread version at startup (traceability of every result).
    const AppSettings s = currentSettings();
    const QString exe = platform::findDwgRead();
    if (exe.isEmpty()) {
        log(tr("dwgread is missing from this installation (expected at %1). Rebuild or reinstall DWG Checker.")
                .arg(QDir::toNativeSeparators(platform::bundledDwgReadPath())));
        return;
    }
    QString err;
    const QString v = DwgReadLoader::probeVersion(exe, &err);
    log(v.isEmpty() ? tr("dwgread at %1 is not usable: %2").arg(exe, err) : tr("parser: %1 at %2").arg(v, QDir::toNativeSeparators(exe)));
    const auto inst = m_acad->current();
    log(inst.exe.isEmpty() ? tr("AutoCAD: not detected (you will be asked to locate it when opening a file)")
                           : tr("AutoCAD: %1 (%2)").arg(inst.name, QDir::toNativeSeparators(inst.exe)));
}

void MainWindow::run() {
    if (m_watcher.isRunning()) return;
    const QStringList refs = m_refCard->paths();
    const QString dir = m_dirCard->path();
    if (refs.isEmpty()) { QMessageBox::warning(this, tr("Run"), tr("Choose the reference drawing first.")); return; }
    for (const QString& r : refs)
        if (!QFileInfo(r).isFile()) { QMessageBox::warning(this, tr("Run"), tr("File not found: %1").arg(r)); return; }
    if (!QFileInfo(dir).isDir()) { QMessageBox::warning(this, tr("Run"), tr("Choose the folder of submissions first.")); return; }
    AppSettings s = currentSettings();
    s.loader.dwgreadPath = platform::findDwgRead();
    auto loader = std::make_shared<AutoLoader>(s.loader);
    BatchInput in;
    in.refPaths = refs;
    in.studentPaths = submissionFiles(dir, refs);
    in.templatePaths = s.duplicates.templateFiles;
    in.settings = s;
    // The views confirmed in the editor, for every drawing whose own mode is Orthographic (the
    // run looks them up per drawing), whatever the mode shown for the set (D76).
    in.views = m_confirmedViews;
    if (in.studentPaths.isEmpty()) { QMessageBox::warning(this, tr("Run"), tr("No .dwg files in %1.").arg(dir)); return; }
    // dwgread is needed unless every file is a JSON dump: any solution, submission or template (D76).
    auto isDwg = [](const QString& p) { return !p.endsWith(QStringLiteral(".json"), Qt::CaseInsensitive); };
    const bool needsDwgRead = std::any_of(refs.cbegin(), refs.cend(), isDwg) ||
                              std::any_of(in.studentPaths.cbegin(), in.studentPaths.cend(), isDwg) ||
                              std::any_of(in.templatePaths.cbegin(), in.templatePaths.cend(), isDwg);
    QString why;
    if (needsDwgRead && !loader->dwgRead().isUsable(&why)) {
        QMessageBox::warning(this, tr("Run"), tr("dwgread is not available: %1").arg(why));
        return;
    }
    {  // the card from the files just found, not a second scan of the folder
        ++m_dirScan;  // a scan still running no longer updates the card
        const DirSummary found = describeFiles(in.studentPaths, s.identityRegex);
        m_dirScanned = dir;
        m_dirCount = found.files;
        m_dirCard->setDetail(found.text, found.problem);
    }
    storeQuizSettings();
    saveState();

    m_runLoader = loader;
    m_cancel = false;
    m_runBtn->setEnabled(false);
    m_refCard->setEnabled(false);
    m_dirCard->setEnabled(false);
    m_cancelBtn->setVisible(true);
    m_progress->setVisible(true);
    m_progress->setRange(0, static_cast<int>(in.studentPaths.size() + in.templatePaths.size() + refs.size()));
    m_progress->setValue(0);
    m_status->setText(tr("Reading…"));
    log(tr("run: %1 submissions, mode %2, parser %3").arg(in.studentPaths.size()).arg(modeName(s.compare.mode), loader->parserVersion()));

    QPointer<MainWindow> self(this);
    auto progress = [self](int done, int total, const QString& what) {
        QMetaObject::invokeMethod(
            qApp, [self, done, total, what]() {
                if (!self) return;
                self->m_progress->setMaximum(total);
                self->m_progress->setValue(done);
                self->m_status->setText(what);
            },
            Qt::QueuedConnection);
    };
    m_watcher.setFuture(QtConcurrent::run([in, loader, progress, this]() {
        try {
            return runBatch(in, *loader, progress, &m_cancel);
        } catch (...) {
            BatchOutput out;
            out.ok = false;
            out.error = QObject::tr("The run stopped on an error: %1").arg(exceptionText(std::current_exception()));
            return out;
        }
    }));
}

void MainWindow::cancel() {
    m_cancel = true;
    m_status->setText(tr("Cancelling…"));
}

void MainWindow::onFinished() {
    m_refCard->setEnabled(true);
    m_dirCard->setEnabled(true);
    m_cancelBtn->setVisible(false);
    updateSteps();
    m_progress->setVisible(false);
    BatchOutput out = m_watcher.result();
    if (!out.ok) {
        m_status->setText(tr("Failed"));
        log(out.error);
        QMessageBox::warning(this, tr("Run"), out.error);
        return;
    }
    for (const QString& l : out.log) log(l);
    for (const auto& r : out.students)
        if (!r.ok()) log(tr("%1: %2").arg(QFileInfo(r.path).fileName(), r.loadError));
    int processed = 0;
    for (const auto& r : out.students) processed += r.ok();
    const int failed = static_cast<int>(out.students.size()) - processed;
    m_status->setText(tr("%1 of %2 files processed%3%4")
                          .arg(processed)
                          .arg(out.students.size())
                          .arg(failed > 0 ? tr("; %1 could not be read (Help > Show log)").arg(failed) : QString())
                          .arg(out.cancelled ? tr(" (cancelled)") : QString()));
    for (auto& d : m_details)
        if (d) d->close();
    m_details.clear();
    m_batch = std::make_shared<const BatchOutput>(std::move(out));
    m_model->setBatch(m_batch);
    m_dups->setBatch(m_batch);
    m_resultsStack->setCurrentIndex(1);
    m_table->sortByColumn(ColName, Qt::AscendingOrder);  // every new batch starts A to Z by student
    m_table->expandAll();
}

void MainWindow::editViews(const QString& chosen) {
    const QStringList refs = m_refCard->paths();
    if (refs.isEmpty()) { QMessageBox::warning(this, tr("Views"), tr("Choose the reference drawing first.")); return; }
    QString refPath = chosen.isEmpty() ? refs.front() : chosen;
    if (chosen.isEmpty() && refs.size() > 1) {  // which solution drawing?
        QStringList names;
        for (const QString& r : refs) names << QFileInfo(r).fileName();
        bool ok = false;
        const QString pick = QInputDialog::getItem(this, tr("Edit views"), tr("Solution drawing:"), names, 0, false, &ok);
        if (!ok) return;
        refPath = refs.value(static_cast<int>(names.indexOf(pick)), refs.front());
    }
    if (!QFileInfo(refPath).isFile()) { QMessageBox::warning(this, tr("Views"), tr("File not found: %1").arg(refPath)); return; }
    AppSettings s = currentSettings();
    // The drawing as read for the card (same reading rules), else read now.
    DrawingPtr ref;
    if (const auto known = m_refDrawings.find(refPath); known != m_refDrawings.end()) {
        ref = known->second;
    } else {
        s.loader.dwgreadPath = platform::findDwgRead();
        AutoLoader loader(s.loader);
        QApplication::setOverrideCursor(Qt::WaitCursor);
        QString err;
        ref = loadDrawing(refPath, loader, s.referenceCanon(), &err);
        QApplication::restoreOverrideCursor();
        if (!ref) { QMessageBox::warning(this, tr("Views"), tr("Could not read the reference: %1").arg(err)); return; }
    }
    // Views confirmed earlier, else what a run would use now: the sidecar, or found with the
    // current settings.
    QString note;
    const auto known = m_confirmedViews.constFind(refPath);
    const ViewSet initial = resolveReferenceViews(*ref, refPath, s.compare,
                                                  known != m_confirmedViews.cend() ? std::optional<ViewSet>(*known) : std::nullopt, &note);
    log(note);
    ViewEditorDialog dlg(ref, refPath, initial, this);
    if (dlg.exec() == QDialog::Accepted) {
        m_confirmedViews.insert(refPath, dlg.viewSet());
        log(dlg.savedToSidecar() ? tr("views saved to %1").arg(QDir::toNativeSeparators(sidecarPath(refPath)))
                                 : tr("views kept for this session only: %1 could not be written")
                                       .arg(QDir::toNativeSeparators(sidecarPath(refPath))));
    }
}

void MainWindow::exportResults() {
    if (!m_batch) { QMessageBox::information(this, tr("Export"), tr("Run a batch first.")); return; }
    const QString p = QFileDialog::getSaveFileName(this, tr("Export results"), QStringLiteral("results.csv"), tr("CSV (*.csv)"));
    if (p.isEmpty()) return;
    QFile f(p);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) { QMessageBox::warning(this, tr("Export"), f.errorString()); return; }
    f.write(QByteArray("\xEF\xBB\xBF"));  // UTF-8 BOM so Excel shows Ø and ⚠ correctly
    f.write(resultsCsv(*m_batch).toUtf8());
    log(tr("exported %1").arg(QDir::toNativeSeparators(p)));
}

void MainWindow::exportDuplicates() {
    if (!m_batch) { QMessageBox::information(this, tr("Export"), tr("Run a batch first.")); return; }
    const QString p = QFileDialog::getSaveFileName(this, tr("Export possible cheating"), QStringLiteral("possible_cheating.csv"), tr("CSV (*.csv)"));
    if (p.isEmpty()) return;
    QFile f(p);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) { QMessageBox::warning(this, tr("Export"), f.errorString()); return; }
    f.write(QByteArray("\xEF\xBB\xBF"));
    f.write(duplicatesCsv(*m_batch).toUtf8());
    log(tr("exported %1").arg(QDir::toNativeSeparators(p)));
}

void MainWindow::openDetail(int student, int errorIndex) {
    if (!m_batch || student < 0 || student >= static_cast<int>(m_batch->students.size())) return;
    if (!m_batch->students[student].ok()) {
        QMessageBox::information(this, tr("Detail"), tr("This file could not be read: %1").arg(m_batch->students[student].loadError));
        return;
    }
    QPointer<DetailDialog> dlg = m_details.value(student);
    const bool opened = !dlg;
    if (!dlg) {
        dlg = new DetailDialog(m_batch, student, this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        m_details.insert(student, dlg);
        dlg->show();
    }
    dlg->raise();
    dlg->activateWindow();
    // A new window opens on the whole drawing with the clicked error selected; an open one zooms to it.
    if (errorIndex >= 0) {
        if (opened) dlg->selectError(errorIndex);
        else dlg->zoomToError(errorIndex);
    }
}

void MainWindow::openInAutoCad(int student) {
    if (!m_batch || student < 0 || student >= static_cast<int>(m_batch->students.size())) return;
    const StudentRun& r = m_batch->students[student];
    const QByteArray sha = r.drawing ? r.drawing->meta.sha256 : QByteArray();
    m_acad->open(this, r.path, sha, [this]() { run(); });
}

}  // namespace dcgui
