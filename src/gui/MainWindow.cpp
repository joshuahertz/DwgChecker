#include "MainWindow.h"

#include "AutoCadController.h"
#include "Canonicalizer.h"
#include "Csv.h"
#include "DetailDialog.h"
#include "DuplicatesTab.h"
#include "PathCard.h"
#include "Platform.h"
#include "ResultsModel.h"
#include "SettingsPanel.h"
#include "ViewEditorDialog.h"

#include <QApplication>
#include <QCloseEvent>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDialog>
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
#include <QSettings>
#include <QSortFilterProxyModel>
#include <QStackedWidget>
#include <QStyle>
#include <QTimer>
#include <QTabWidget>
#include <QTreeView>
#include <QInputDialog>
#include <QSet>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

using namespace dc;

namespace dcgui {

namespace {

// The reading rules (layers, paper space, ...) as text, to notice when they change.
QByteArray canonSignature(const AppSettings& s) {
    return QJsonDocument(s.toJson().value(QStringLiteral("canon")).toObject()).toJson(QJsonDocument::Compact);
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
    auto* lay = new QVBoxLayout(central);

    // ---- Top: the reference and the submissions as cards, and Run ----
    auto* top = new QHBoxLayout;
    m_refCard = new PathCard(PathCard::Kind::File, 1, tr("Reference (solution)"), tr("Click to choose the reference drawing"),
                             tr("Drawings (*.dwg *.json)"), {QStringLiteral("dwg"), QStringLiteral("json")}, true, central);
    m_dirCard = new PathCard(PathCard::Kind::Folder, 2, tr("Student submissions"), tr("Click to choose the submissions folder"),
                             QString(), {}, false, central);
    top->addWidget(m_refCard, 1);
    top->addWidget(m_dirCard, 1);
    m_runBtn = new QPushButton(tr("Run"), central);
    m_runBtn->setDefault(true);
    m_runBtn->setIconSize(QSize(24, 24));  // the step-3 badge (updateSteps)
    QFont runFont = m_runBtn->font();
    runFont.setBold(true);
    runFont.setPointSizeF(runFont.pointSizeF() * 1.2);
    m_runBtn->setFont(runFont);
    m_runBtn->setMinimumSize(120, 48);
    m_cancelBtn = new QPushButton(tr("Cancel"), central);
    m_cancelBtn->setVisible(false);  // shown while a batch runs
    auto* runCol = new QVBoxLayout;
    runCol->addWidget(m_runBtn);
    runCol->addWidget(m_cancelBtn);
    runCol->addStretch();
    top->addLayout(runCol);
    lay->addLayout(top);

    auto* statusRow = new QHBoxLayout;
    m_settingsToggle = new QToolButton(central);
    m_settingsToggle->setText(tr("Settings"));
    m_settingsToggle->setCheckable(true);
    m_settingsToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_settingsToggle->setArrowType(Qt::RightArrow);
    m_progress = new QProgressBar(central);
    m_progress->setVisible(false);
    m_status = new QLabel(central);
    statusRow->addWidget(m_settingsToggle);
    statusRow->addStretch();
    statusRow->addWidget(m_status);
    statusRow->addWidget(m_progress, 1);
    lay->addLayout(statusRow);

    m_settings = new SettingsPanel(m_acad, central);
    m_settingsArea = new QScrollArea(central);
    m_settingsArea->setWidget(m_settings);
    m_settingsArea->setWidgetResizable(true);
    m_settingsArea->setVisible(false);  // its height follows the panel (fitSettingsArea)
    lay->addWidget(m_settingsArea);

    // ---- Tabs ----
    m_tabs = new QTabWidget(central);
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
        const QColor base = pal.color(QPalette::Base), text = pal.color(QPalette::Text);
        const double t = 0.045;
        pal.setColor(QPalette::AlternateBase, QColor::fromRgbF(static_cast<float>(base.redF() * (1 - t) + text.redF() * t),
                                                               static_cast<float>(base.greenF() * (1 - t) + text.greenF() * t),
                                                               static_cast<float>(base.blueF() * (1 - t) + text.blueF() * t)));
        m_table->setPalette(pal);
    }
    m_resultsStack = new QStackedWidget(resultsPage);
    m_guide = new QLabel(m_resultsStack);
    m_guide->setAlignment(Qt::AlignCenter);
    m_guide->setTextFormat(Qt::RichText);
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
    exportRow->addWidget(tableHint);
    exportRow->addStretch();
    exportRow->addWidget(exportBtn);
    rl->addLayout(exportRow);
    m_tabs->addTab(resultsPage, tr("Results"));

    auto* dupPage = new QWidget(m_tabs);
    auto* dl = new QVBoxLayout(dupPage);
    m_dups = new DuplicatesTab(dupPage);
    dl->addWidget(m_dups);
    auto* dupExportRow = new QHBoxLayout;
    auto* dupExport = new QPushButton(tr("Export duplicates CSV…"), dupPage);
    dupExportRow->addStretch();
    dupExportRow->addWidget(dupExport);
    dl->addLayout(dupExportRow);
    m_tabs->addTab(dupPage, tr("Duplicates"));

    lay->addWidget(m_tabs, 1);

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
    file->addAction(tr("Export duplicates CSV…"), this, &MainWindow::exportDuplicates);
    file->addSeparator();
    file->addAction(tr("Save settings as JSON…"), this, [this]() {
        const QString p = QFileDialog::getSaveFileName(this, tr("Save settings"), QString(), tr("JSON (*.json)"));
        if (p.isEmpty()) return;
        QFile f(p);
        if (f.open(QIODevice::WriteOnly)) f.write(QJsonDocument(currentSettings().toJson()).toJson());
    });
    file->addAction(tr("Load settings from JSON…"), this, [this]() {
        const QString p = QFileDialog::getOpenFileName(this, tr("Load settings"), QString(), tr("JSON (*.json)"));
        if (p.isEmpty()) return;
        QFile f(p);
        if (!f.open(QIODevice::ReadOnly)) return;
        const AppSettings s = AppSettings::fromJson(QJsonDocument::fromJson(f.readAll()).object());
        m_settings->setSettings(s);
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
        QMessageBox::about(this, tr("About DWG Checker"),
                           tr("<b>DWG Checker %1</b><br>Grades CAD assignments against a reference drawing and shows evidence of "
                              "shared files.<br><br>Parser: %2<br>LibreDWG pinned version: %3<br>Qt %4")
                               .arg(QStringLiteral(DWGCHECK_VERSION),
                                    m_runLoader ? m_runLoader->parserVersion().toHtmlEscaped() : tr("see Log"),
                                    QStringLiteral(DWGCHECK_LIBREDWG_VERSION), QString::fromLatin1(qVersion())));
    });

    // ---- Wiring ----
    connect(m_refCard, &PathCard::pathsChanged, this, &MainWindow::referenceChanged);
    connect(m_dirCard, &PathCard::pathsChanged, this, &MainWindow::folderChanged);
    connect(m_refCard, &PathCard::clearRecentRequested, this, [this]() { clearRecent(kRecentRefs, m_refCard); });
    connect(m_dirCard, &PathCard::clearRecentRequested, this, [this]() { clearRecent(kRecentDirs, m_dirCard); });
    connect(m_settingsToggle, &QToolButton::toggled, this, [this](bool on) {
        m_settingsArea->setVisible(on);
        m_settingsToggle->setArrowType(on ? Qt::DownArrow : Qt::RightArrow);
        fitSettingsArea();
    });
    connect(m_settings, &SettingsPanel::sizeChanged, this, &MainWindow::fitSettingsArea);
    connect(m_settings, &SettingsPanel::changed, this, &MainWindow::storeQuizSettings);
    connect(m_settings, &SettingsPanel::changed, this, [this]() {  // the solution card's counts follow the reading rules
        if (canonSignature(currentSettings()) != m_refCanonSig) updateRefInfo();
    });
    connect(m_settings, &SettingsPanel::editViewsRequested, this, &MainWindow::editViews);
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
    connect(m_table->header(), &QHeaderView::sectionResized, m_table, [this]() { m_table->doItemsLayout(); });
}

AppSettings MainWindow::currentSettings() const { return m_settings->settings(); }

void MainWindow::loadState() {
    QSettings q;
    const AppSettings s = AppSettings::fromJson(QJsonDocument::fromJson(q.value("settings/json").toByteArray()).object());
    m_settings->setSettings(s);
    m_refCard->setRecent(q.value(kRecentRefs).toStringList());
    m_dirCard->setRecent(q.value(kRecentDirs).toStringList());
    // The app opens with nothing chosen: step 1 is next. Recent choices are one click away.
    referenceChanged();
    folderChanged();
    restoreGeometry(q.value("window/geometry").toByteArray());
}

void MainWindow::saveState() const {
    QSettings q;
    q.setValue("settings/json", QJsonDocument(currentSettings().toJson()).toJson(QJsonDocument::Compact));
    q.setValue("window/geometry", saveGeometry());
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

// Settings are remembered per assignment: the set of solution drawings, by full path.
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

// "inches  ·  4 objects  ·  5 dimensions", or per drawing when there are several;
// read in the background.
QPair<bool, QString> describeReferences(const QStringList& paths, const AppSettings& s) {
    AutoLoader loader(s.loader);
    QStringList parts, units;
    for (const QString& path : paths) {
        QString err;
        const auto d = loadDrawing(path, loader, s.referenceCanon(), &err);
        if (!d) return {false, QObject::tr("could not read %1: %2").arg(QFileInfo(path).fileName(), err)};
        int objects = 0, dims = 0, hatches = 0;
        for (const auto& p : d->prims) {
            objects += isGeometric(p.kind);
            dims += p.kind == PrimKind::Dimension;
            hatches += p.kind == PrimKind::Hatch;
        }
        QStringList counts{countText(objects, QObject::tr("object"), QObject::tr("objects"))};
        if (dims > 0) counts << countText(dims, QObject::tr("dimension"), QObject::tr("dimensions"));
        if (hatches > 0) counts << countText(hatches, QObject::tr("hatch"), QObject::tr("hatches"));
        for (const auto& w : d->warnings)  // e.g. a sketching grid the instructor turned off
            if (w.kind == WarnKind::HiddenLayer) counts << w.text;
        if (d->meta.insUnits > 0 && !units.contains(insUnitsName(d->meta.insUnits))) units << insUnitsName(d->meta.insUnits);
        parts << (paths.size() > 1 ? QStringLiteral("%1: %2").arg(QFileInfo(path).completeBaseName(), counts.join(QStringLiteral(", ")))
                                   : counts.join(QStringLiteral("  ·  ")));
    }
    QString text = parts.join(QStringLiteral("  ·  "));
    if (!units.isEmpty()) text = units.join(QStringLiteral(" / ")) + QStringLiteral("  ·  ") + text;
    return {true, text};
}

}  // namespace

void MainWindow::referenceChanged() {
    const QStringList paths = m_refCard->paths();
    addRecent(kRecentRefs, m_refCard, PathCard::encode(paths));
    m_refReadable = 0;
    m_settings->setSolutionDrawings(paths);  // several: each gets its own mode and checks
    if (paths.isEmpty()) {
        m_refCard->setCaptionNote(QString());
        m_refCard->setDetail(tr("The solution drawing the submissions are graded against; choose several for a multi-drawing "
                                "assignment (or drop them here)"));
        updateDirInfo();
        return;
    }
    const QByteArray saved = QSettings().value(quizKey(paths)).toByteArray();
    if (!saved.isEmpty()) {
        m_settings->setSettings(AppSettings::fromJson(QJsonDocument::fromJson(saved).object()));
        m_refCard->setCaptionNote(tr("settings saved for this quiz"));
        log(tr("loaded the settings saved for %1").arg(m_refCard->paths().size() > 1 ? tr("this set of drawings")
                                                                                       : QFileInfo(paths.front()).fileName()));
    } else {
        m_refCard->setCaptionNote(tr("new quiz: check the settings"));
    }
    updateRefInfo();
    updateDirInfo();
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
            m_refReadable = -1;
            m_refCard->setDetail(tr("file not found: %1").arg(QDir::toNativeSeparators(p)), true);
            updateSteps();
            return;
        }
    m_refCard->setDetail(tr("reading…"));
    AppSettings s = currentSettings();
    m_refCanonSig = canonSignature(s);
    s.loader.dwgreadPath = platform::findDwgRead();
    auto* w = new QFutureWatcher<QPair<bool, QString>>(this);
    connect(w, &QFutureWatcherBase::finished, this, [this, w, paths]() {
        const QPair<bool, QString> res = w->result();
        w->deleteLater();
        if (paths != m_refCard->paths()) return;  // a newer choice is being read
        m_refReadable = res.first ? 1 : -1;
        m_refCard->setDetail(res.second, !res.first);
        updateSteps();
    });
    w->setFuture(QtConcurrent::run([paths, s]() { return describeReferences(paths, s); }));
}

void MainWindow::updateDirInfo() {
    const QString dir = m_dirCard->path();
    m_dirCount = 0;
    if (dir.isEmpty()) {
        m_dirCard->setDetail(tr("The folder with the students' drawings; a Canvas download works as-is (or drop it here)"));
        updateSteps();
        return;
    }
    if (!QFileInfo(dir).isDir()) {
        m_dirCard->setDetail(tr("folder not found"), true);
        updateSteps();
        return;
    }
    QStringList files = findSubmissionFiles(dir);
    for (const QString& r : m_refCard->paths()) files.removeAll(QFileInfo(r).absoluteFilePath());
    const int n = static_cast<int>(files.size());
    m_dirCount = n;
    // Files are grouped by student as in a run (name + Canvas user id).
    const IdentityParser idp(currentSettings().identityRegex);
    QSet<QString> students;
    for (const QString& f : files) students.insert(studentKey(idp.parse(f)));
    const bool json = n > 0 && files.front().endsWith(QStringLiteral(".json"), Qt::CaseInsensitive);
    const QString what = json ? countText(n, tr("JSON dump"), tr("JSON dumps")) : countText(n, tr("drawing"), tr("drawings"));
    if (n == 0) m_dirCard->setDetail(tr("no drawings in this folder"), true);
    else if (students.size() < n) m_dirCard->setDetail(tr("%1 from %2").arg(what, countText(students.size(), tr("student"), tr("students"))));
    else m_dirCard->setDetail(tr("%1 found").arg(what));
    updateSteps();
}

void MainWindow::updateSteps() {
    using State = PathCard::State;
    const bool running = m_watcher.isRunning();
    const bool refDone = !m_refCard->path().isEmpty() && m_refReadable >= 0;  // still reading counts as chosen
    const bool dirDone = m_dirCount > 0;
    const bool ready = refDone && dirDone;
    m_refCard->setState(refDone ? State::Done : State::Active);
    m_dirCard->setState(dirDone ? State::Done : refDone ? State::Active : State::Waiting);
    m_runBtn->setEnabled(ready && !running);
    m_runBtn->setIcon(QIcon(stepBadge(3, ready ? State::Active : State::Waiting, 24, devicePixelRatioF())));
    m_runBtn->setToolTip(ready    ? tr("Step 3: grade the %1 drawings").arg(m_dirCount)
                         : !refDone ? tr("First choose the reference drawing (step 1)")
                                    : tr("Next choose the submissions folder (step 2)"));

    // The guide shown in Results until the first run: done steps checked, the next one in bold.
    const int next = !refDone ? 1 : !dirDone ? 2 : 3;
    auto line = [next](int n, bool done, const QString& text) {
        QString t = QStringLiteral("%1&nbsp;&nbsp;%2").arg(done ? QStringLiteral("&#10003;") : QString::number(n), text);
        if (n == next) t = QStringLiteral("<b>%1</b>").arg(t);
        else if (done) t = QStringLiteral("<span style='color:gray'>%1</span>").arg(t);
        return QStringLiteral("<p>%1</p>").arg(t);
    };
    m_guide->setText(QStringLiteral("<h2>%1</h2>").arg(tr("Grade a set of drawings in three steps")) +
                     line(1, refDone, tr("Choose the reference (solution) drawing, or several for a multi-drawing assignment: "
                                         "click the first card above.")) +
                     line(2, dirDone, tr("Choose the folder of student submissions: click the second card.")) +
                     line(3, false, tr("Click <b>Run</b>. The results appear here; click a row for its details.")));
}

// The settings area is as tall as the panel needs (Advanced open or not, the drawings
// table shown or not), up to a limit beyond which it scrolls. Laid out again first, and
// once more after pending layout requests, so a collapse never leaves blank space.
void MainWindow::fitSettingsArea() {
    constexpr int kMaxHeight = 560;
    auto fit = [this]() {
        if (QLayout* l = m_settings->layout()) l->activate();
        const int h = m_settings->sizeHint().height() + 2 * m_settingsArea->frameWidth();
        m_settingsArea->setFixedHeight(std::min(h, kMaxHeight));
    };
    fit();
    QTimer::singleShot(0, this, fit);
}

void MainWindow::storeQuizSettings() {
    const QStringList refs = m_refCard->paths();
    if (refs.isEmpty()) return;
    QSettings().setValue(quizKey(refs), QJsonDocument(currentSettings().toJson()).toJson(QJsonDocument::Compact));
    m_refCard->setCaptionNote(tr("settings saved for this quiz"));
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
    const QString ref = m_refCard->path();
    const QString dir = m_dirCard->path();
    if (refs.isEmpty()) { QMessageBox::warning(this, tr("Run"), tr("Choose the reference drawing first.")); return; }
    for (const QString& r : refs)
        if (!QFileInfo(r).isFile()) { QMessageBox::warning(this, tr("Run"), tr("File not found: %1").arg(r)); return; }
    if (!QFileInfo(dir).isDir()) { QMessageBox::warning(this, tr("Run"), tr("Choose the folder of submissions first.")); return; }
    AppSettings s = currentSettings();
    s.loader.dwgreadPath = platform::findDwgRead();
    auto loader = std::make_shared<AutoLoader>(s.loader);
    QString why;
    if (!loader->dwgRead().isUsable(&why) && !ref.endsWith(".json", Qt::CaseInsensitive)) {
        QMessageBox::warning(this, tr("Run"), tr("dwgread is not available: %1").arg(why));
        return;
    }
    BatchInput in;
    in.refPaths = refs;
    in.studentPaths = findSubmissionFiles(dir);
    for (const QString& r : refs) in.studentPaths.removeAll(QFileInfo(r).absoluteFilePath());
    in.templatePaths = s.duplicates.templateFiles;
    in.settings = s;
    if (s.compare.mode == Mode::Orthographic) in.views = m_viewsByRef;
    if (in.studentPaths.isEmpty()) { QMessageBox::warning(this, tr("Run"), tr("No .dwg files in %1.").arg(dir)); return; }
    updateDirInfo();
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
    m_watcher.setFuture(QtConcurrent::run([in, loader, progress, this]() { return runBatch(in, *loader, progress, &m_cancel); }));
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
    if (out.hasViews)  // keep the auto/sidecar views for the editor
        for (int k = 0; k < static_cast<int>(out.refPaths.size()); ++k)
            if (!m_viewsByRef.contains(out.refPaths[k])) m_viewsByRef.insert(out.refPaths[k], out.views[k]);
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
    s.loader.dwgreadPath = platform::findDwgRead();
    AutoLoader loader(s.loader);
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QString err;
    auto ref = loadDrawing(refPath, loader, s.referenceCanon(), &err);
    QApplication::restoreOverrideCursor();
    if (!ref) { QMessageBox::warning(this, tr("Views"), tr("Could not read the reference: %1").arg(err)); return; }
    QString note;
    const auto known = m_viewsByRef.constFind(refPath);
    const ViewSet initial = resolveReferenceViews(*ref, refPath, s.compare,
                                                  known != m_viewsByRef.cend() ? std::optional<ViewSet>(*known) : std::nullopt, &note);
    log(note);
    ViewEditorDialog dlg(ref, refPath, initial, this);
    if (dlg.exec() == QDialog::Accepted) {
        m_viewsByRef.insert(refPath, dlg.viewSet());
        log(tr("views saved to %1").arg(QDir::toNativeSeparators(sidecarPath(refPath))));
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
    const QString p = QFileDialog::getSaveFileName(this, tr("Export duplicates"), QStringLiteral("duplicates.csv"), tr("CSV (*.csv)"));
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
