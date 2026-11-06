#include "MainWindow.h"

#include "AutoCadController.h"
#include "Canonicalizer.h"
#include "Csv.h"
#include "DetailDialog.h"
#include "DuplicatesTab.h"
#include "Platform.h"
#include "ResultsModel.h"
#include "SettingsPanel.h"
#include "ViewEditorDialog.h"

#include <QApplication>
#include <QCloseEvent>
#include <QDateTime>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonDocument>
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
#include <QTabWidget>
#include <QTableView>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

using namespace dc;

namespace dcgui {

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

    // ---- Top bar ----
    auto* grid = new QGridLayout;
    m_refEdit = new QLineEdit(central);
    m_refEdit->setPlaceholderText(tr("reference (solution) DWG"));
    auto* refBrowse = new QPushButton(tr("Browse…"), central);
    m_viewsBtn = new QPushButton(tr("Edit views…"), central);
    m_viewsBtn->setToolTip(tr("Orthographic mode: confirm or fix how the reference splits into views"));
    grid->addWidget(new QLabel(tr("Reference:"), central), 0, 0);
    grid->addWidget(m_refEdit, 0, 1);
    grid->addWidget(refBrowse, 0, 2);
    grid->addWidget(m_viewsBtn, 0, 3);
    m_dirEdit = new QLineEdit(central);
    m_dirEdit->setPlaceholderText(tr("folder of student submissions"));
    auto* dirBrowse = new QPushButton(tr("Browse…"), central);
    grid->addWidget(new QLabel(tr("Student folder:"), central), 1, 0);
    grid->addWidget(m_dirEdit, 1, 1);
    grid->addWidget(dirBrowse, 1, 2);
    grid->setColumnStretch(1, 1);
    lay->addLayout(grid);

    auto* runRow = new QHBoxLayout;
    m_settingsToggle = new QToolButton(central);
    m_settingsToggle->setText(tr("Settings"));
    m_settingsToggle->setCheckable(true);
    m_settingsToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_settingsToggle->setArrowType(Qt::RightArrow);
    m_runBtn = new QPushButton(tr("Run"), central);
    m_runBtn->setDefault(true);
    m_cancelBtn = new QPushButton(tr("Cancel"), central);
    m_cancelBtn->setEnabled(false);
    m_progress = new QProgressBar(central);
    m_progress->setVisible(false);
    m_status = new QLabel(central);
    runRow->addWidget(m_settingsToggle);
    runRow->addStretch();
    runRow->addWidget(m_status);
    runRow->addWidget(m_progress, 1);
    runRow->addWidget(m_runBtn);
    runRow->addWidget(m_cancelBtn);
    lay->addLayout(runRow);

    m_settings = new SettingsPanel(m_acad, central);
    m_settingsArea = new QScrollArea(central);
    m_settingsArea->setWidget(m_settings);
    m_settingsArea->setWidgetResizable(true);
    m_settingsArea->setMaximumHeight(560);
    m_settingsArea->setVisible(false);
    lay->addWidget(m_settingsArea);

    // ---- Tabs ----
    m_tabs = new QTabWidget(central);
    auto* resultsPage = new QWidget(m_tabs);
    auto* rl = new QVBoxLayout(resultsPage);
    m_table = new QTableView(resultsPage);
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
    m_table->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(ColErrors, QHeaderView::Stretch);
    m_table->setColumnWidth(ColName, 260);
    m_table->setColumnWidth(ColAcad, 130);
    m_table->setColumnWidth(ColFlag, 240);
    m_table->setColumnWidth(ColCorrectness, 110);
    rl->addWidget(m_table);
    auto* exportRow = new QHBoxLayout;
    auto* exportBtn = new QPushButton(tr("Export results CSV…"), resultsPage);
    auto* tableHint = new QLabel(tr("Click a row to see its errors in the detail view; click \"Open in AutoCAD\" to open the "
                                    "file there."),
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

    m_log = new QPlainTextEdit(m_tabs);
    m_log->setReadOnly(true);
    m_tabs->addTab(m_log, tr("Log"));
    lay->addWidget(m_tabs, 1);
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
        updateModeUi();
    });
    file->addSeparator();
    file->addAction(tr("Quit"), this, &QWidget::close);
    QMenu* help = menuBar()->addMenu(tr("&Help"));
    help->addAction(tr("About"), this, [this]() {
        QMessageBox::about(this, tr("About DWG Checker"),
                           tr("<b>DWG Checker %1</b><br>Grades CAD assignments against a reference drawing and shows evidence of "
                              "shared files.<br><br>Parser: %2<br>LibreDWG pinned version: %3<br>Qt %4")
                               .arg(QStringLiteral(DWGCHECK_VERSION),
                                    m_runLoader ? m_runLoader->parserVersion().toHtmlEscaped() : tr("see Log"),
                                    QStringLiteral(DWGCHECK_LIBREDWG_VERSION), QString::fromLatin1(qVersion())));
    });

    // ---- Wiring ----
    connect(refBrowse, &QPushButton::clicked, this, [this]() {
        const QString p = QFileDialog::getOpenFileName(this, tr("Reference drawing"), m_refEdit->text(), tr("Drawings (*.dwg *.json)"));
        if (!p.isEmpty()) { m_refEdit->setText(p); m_views.reset(); }
    });
    connect(dirBrowse, &QPushButton::clicked, this, [this]() {
        const QString p = QFileDialog::getExistingDirectory(this, tr("Student submissions"), m_dirEdit->text());
        if (!p.isEmpty()) m_dirEdit->setText(p);
    });
    connect(m_refEdit, &QLineEdit::textEdited, this, [this]() { m_views.reset(); });
    connect(m_settingsToggle, &QToolButton::toggled, this, [this](bool on) {
        m_settingsArea->setVisible(on);
        m_settingsToggle->setArrowType(on ? Qt::DownArrow : Qt::RightArrow);
    });
    connect(m_settings, &SettingsPanel::changed, this, &MainWindow::updateModeUi);
    connect(m_runBtn, &QPushButton::clicked, this, &MainWindow::run);
    connect(m_cancelBtn, &QPushButton::clicked, this, &MainWindow::cancel);
    connect(m_viewsBtn, &QPushButton::clicked, this, &MainWindow::editViews);
    connect(exportBtn, &QPushButton::clicked, this, &MainWindow::exportResults);
    connect(dupExport, &QPushButton::clicked, this, &MainWindow::exportDuplicates);
    connect(m_table, &QTableView::clicked, this, [this](const QModelIndex& idx) {
        const int student = m_proxy->mapToSource(idx).data(StudentIndexRole).toInt();
        if (idx.column() == ColAcad) openInAutoCad(student);
        else openDetail(student);
    });
    m_table->viewport()->setCursor(Qt::PointingHandCursor);  // every cell opens something
    connect(m_table->horizontalHeader(), &QHeaderView::sectionResized, m_table, [this]() { m_table->resizeRowsToContents(); });
}

AppSettings MainWindow::currentSettings() const { return m_settings->settings(); }

void MainWindow::loadState() {
    QSettings q;
    m_refEdit->setText(q.value("paths/ref").toString());
    m_dirEdit->setText(q.value("paths/studentDir").toString());
    const AppSettings s = AppSettings::fromJson(QJsonDocument::fromJson(q.value("settings/json").toByteArray()).object());
    m_settings->setSettings(s);
    restoreGeometry(q.value("window/geometry").toByteArray());
    updateModeUi();
}

void MainWindow::saveState() const {
    QSettings q;
    q.setValue("paths/ref", m_refEdit->text());
    q.setValue("paths/studentDir", m_dirEdit->text());
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

void MainWindow::updateModeUi() { m_viewsBtn->setVisible(m_settings->settings().compare.mode == Mode::Orthographic); }

void MainWindow::run() {
    if (m_watcher.isRunning()) return;
    const QString ref = m_refEdit->text().trimmed();
    const QString dir = m_dirEdit->text().trimmed();
    if (!QFileInfo(ref).isFile()) { QMessageBox::warning(this, tr("Run"), tr("Choose the reference drawing first.")); return; }
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
    in.refPath = ref;
    in.studentPaths = findSubmissionFiles(dir);
    in.studentPaths.removeAll(QFileInfo(ref).absoluteFilePath());
    in.templatePaths = s.duplicates.templateFiles;
    in.settings = s;
    if (s.compare.mode == Mode::Orthographic && m_views) in.views = m_views;
    if (in.studentPaths.isEmpty()) { QMessageBox::warning(this, tr("Run"), tr("No .dwg files in %1.").arg(dir)); return; }
    saveState();

    m_runLoader = loader;
    m_batchRefPath = ref;
    m_cancel = false;
    m_runBtn->setEnabled(false);
    m_cancelBtn->setEnabled(true);
    m_progress->setVisible(true);
    m_progress->setRange(0, static_cast<int>(in.studentPaths.size() + in.templatePaths.size() + 1));
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
    m_runBtn->setEnabled(true);
    m_cancelBtn->setEnabled(false);
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
    if (out.hasViews && !m_views) m_views = out.views;  // keep auto/sidecar views for the editor
    int processed = 0;
    for (const auto& r : out.students) processed += r.ok();
    m_status->setText(tr("%1 of %2 files processed%3")
                          .arg(processed)
                          .arg(out.students.size())
                          .arg(out.cancelled ? tr(" (cancelled)") : QString()));
    for (auto& d : m_details)
        if (d) d->close();
    m_details.clear();
    m_batch = std::make_shared<const BatchOutput>(std::move(out));
    m_model->setBatch(m_batch);
    m_dups->setBatch(m_batch);
    m_table->resizeRowsToContents();
}

void MainWindow::editViews() {
    const QString refPath = m_refEdit->text().trimmed();
    if (!QFileInfo(refPath).isFile()) { QMessageBox::warning(this, tr("Views"), tr("Choose the reference drawing first.")); return; }
    AppSettings s = currentSettings();
    s.loader.dwgreadPath = platform::findDwgRead();
    AutoLoader loader(s.loader);
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QString err;
    auto ref = loadDrawing(refPath, loader, s.effectiveCanon(), &err);
    QApplication::restoreOverrideCursor();
    if (!ref) { QMessageBox::warning(this, tr("Views"), tr("Could not read the reference: %1").arg(err)); return; }
    QString note;
    const ViewSet initial = resolveReferenceViews(*ref, refPath, s.compare, m_views, &note);
    log(note);
    ViewEditorDialog dlg(ref, refPath, initial, this);
    if (dlg.exec() == QDialog::Accepted) {
        m_views = dlg.viewSet();
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
    f.write(resultsCsv(*m_batch, m_batchRefPath).toUtf8());
    log(tr("exported %1").arg(QDir::toNativeSeparators(p)));
}

void MainWindow::exportDuplicates() {
    if (!m_batch) { QMessageBox::information(this, tr("Export"), tr("Run a batch first.")); return; }
    const QString p = QFileDialog::getSaveFileName(this, tr("Export duplicates"), QStringLiteral("duplicates.csv"), tr("CSV (*.csv)"));
    if (p.isEmpty()) return;
    QFile f(p);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) { QMessageBox::warning(this, tr("Export"), f.errorString()); return; }
    f.write(QByteArray("\xEF\xBB\xBF"));
    f.write(duplicatesCsv(*m_batch, m_batchRefPath).toUtf8());
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
