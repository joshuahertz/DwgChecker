#include "DetailDialog.h"

#include "DrawingView.h"
#include "Csv.h"
#include "Format.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFileInfo>
#include <QGraphicsItem>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

using namespace dc;

namespace dcgui {

namespace {
// Overlay: matched green, missing red, present but wrong (shifted, wrong size...) yellow, extra purple.
const QColor kRefGray(175, 175, 175), kMatched(30, 150, 50), kMissing(210, 30, 30), kShifted(225, 175, 0), kExtra(150, 70, 200);
const QColor kDarkText(20, 20, 20);  // on the light status colors
}

DetailDialog::DetailDialog(std::shared_ptr<const BatchOutput> batch, int student, QWidget* parent)
    : QDialog(parent), m_batch(std::move(batch)), m_student(student) {
    const StudentRun& run = m_batch->students[m_student];
    setWindowTitle(tr("Detail: %1").arg(QFileInfo(run.path).fileName()));
    resize(1300, 820);
    auto* lay = new QVBoxLayout(this);

    const CompareResult& r = run.result;
    QString summary = tr("<b>%1</b> — %2 — %3  (%4 of %5 matched, %6 penalized extras)")
                          .arg(run.identity.name.toHtmlEscaped(), correctnessText(run), r.errorSummary().toHtmlEscaped())
                          .arg(r.matched).arg(r.refCount).arg(r.penalizedExtras);
    const QString refName = QFileInfo(m_batch->refPaths.value(m_batch->refOf(run))).fileName().toHtmlEscaped();
    if (!run.matched())
        summary += tr(" — <b>extra file, not graded</b>: shown against the closest solution drawing, %1").arg(refName);
    else if (m_batch->multipleRefs())
        summary += tr(" — solution drawing: <b>%1</b>").arg(refName);
    if (r.scaled) summary += tr(" — <b>scaled</b>: compared after dividing by %1").arg(QString::number(r.scaleFactor, 'g', 6));
    if (r.moved) summary += tr(" — <b>moved back</b> by dx %1, dy %2 before grading").arg(fmtCoord(r.dx), fmtCoord(r.dy));
    if (r.mode == Mode::Relative)
        summary += tr(" — transform: dx %1, dy %2, θ %3%4")
                       .arg(fmtCoord(r.dx), fmtCoord(r.dy), fmtDeg(r.theta), r.mirrored ? tr(", mirrored") : QString());
    auto* head = new QLabel(summary, this);
    lay->addWidget(head);
    auto* legend = new QLabel(tr("<span style='color:#afafaf'>■</span> reference &nbsp; <span style='color:#1e9632'>■</span> matched "
                                 "&nbsp; <span style='color:#d21e1e'>■</span> missing (where it should be) &nbsp; "
                                 "<span style='color:#e1af00'>■</span> present but wrong: shifted, wrong size… &nbsp; "
                                 "<span style='color:#9646c8'>■</span> extra &nbsp; (wheel: zoom, drag: pan)"),
                              this);
    lay->addWidget(legend);

    auto* split = new QSplitter(Qt::Horizontal, this);
    lay->addWidget(split, 1);
    m_view = new DrawingView(split);
    split->addWidget(m_view);

    auto* tabs = new QTabWidget(split);
    m_errors = new QListWidget(tabs);
    m_errors->setWordWrap(true);
    for (const auto& e : r.errors) m_errors->addItem(e.text);
    tabs->addTab(m_errors, tr("Discrepancies (%1)").arg(r.errors.size()));
    if (r.mode == Mode::Orthographic && m_batch->viewsOf(run)) {  // only for a drawing graded in Orthographic mode
        auto* views = new QTableWidget(tabs);
        buildViews(views);
        tabs->addTab(views, tr("Views"));
    }
    auto* meta = new QTableWidget(tabs);
    buildMetadata(meta);
    tabs->addTab(meta, tr("Metadata"));
    tabs->addTab(buildPrimitivesPage(tabs), tr("Primitives"));
    split->addWidget(tabs);
    // Opens as a 50/50 split of drawing and lists, and keeps that ratio when resized.
    // Equal sizes are scaled to the real width once the dialog is laid out.
    split->setStretchFactor(0, 1);
    split->setStretchFactor(1, 1);
    split->setSizes({1000, 1000});

    auto* buttons = new QHBoxLayout;
    auto* all = new QPushButton(tr("Zoom to all"), this);
    auto* close = new QPushButton(tr("Close"), this);
    buttons->addWidget(all);
    buttons->addStretch();
    buttons->addWidget(close);
    lay->addLayout(buttons);
    connect(all, &QPushButton::clicked, m_view, &DrawingView::zoomToAll);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_errors, &QListWidget::currentRowChanged, this, &DetailDialog::zoomToError);
    // Clicking the row that is already current (e.g. the error the window was opened for) zooms too.
    connect(m_errors, &QListWidget::itemClicked, this, [this](QListWidgetItem* it) { zoomToError(m_errors->row(it)); });

    buildOverlay();  // ends with zoom to all
    // The drawing takes the initial focus: a list that receives keyboard focus with no
    // current row makes its first row current, which would zoom to the first error.
    m_view->setFocus(Qt::OtherFocusReason);
}

void DetailDialog::selectError(int k) {
    if (k < 0 || k >= m_errors->count()) return;
    const QSignalBlocker block(m_errors);
    m_errors->setCurrentRow(k);
    m_errors->scrollToItem(m_errors->item(k));
}

// A view's box and title take its status color: not found or something missing red,
// something wrong (or a failed alignment check) yellow, only extras purple, else green.
QColor DetailDialog::viewColor(int v) const {
    const StudentRun& run = m_batch->students[m_student];
    const CompareResult& r = run.result;
    const ViewSet* vs = m_batch->viewsOf(run);
    if (!vs || v >= static_cast<int>(r.views.size()) || !r.views[v].found) return kMissing;
    int missing = 0, wrong = 0;
    for (int i : vs->views[v].prims) {
        if (i < 0 || i >= static_cast<int>(r.refStatus.size())) continue;
        missing += r.refStatus[i] == PrimStatus::Missing;
        wrong += r.refStatus[i] == PrimStatus::NearMiss || r.refStatus[i] == PrimStatus::WrongAttribute;
    }
    bool check = false;
    for (const auto& e : r.errors)
        if ((e.cat == ErrCat::Alignment || e.cat == ErrCat::Arrangement) && e.view == v && e.failing) check = true;
    if (missing > 0) return kMissing;
    if (wrong > 0 || check) return kShifted;
    if (r.views[v].extras > 0) return kExtra;
    return kMatched;
}

void DetailDialog::buildOverlay() {
    const StudentRun& run = m_batch->students[m_student];
    const Drawing& ref = m_batch->refDrawing(run);
    const CompareResult& r = run.result;
    m_view->clearDrawing();
    // Reference underneath in gray; missing/wrong reference primitives in red on top.
    for (int i = 0; i < static_cast<int>(ref.prims.size()); ++i) {
        const PrimStatus st = r.refStatus[i];
        if (st == PrimStatus::Missing || st == PrimStatus::NearMiss || st == PrimStatus::WrongAttribute)
        {
            if (auto* it = m_view->addPrimitive(ref.prims[i], kMissing, 2.0)) it->setZValue(2);
        } else if (auto* it = m_view->addPrimitive(ref.prims[i], kRefGray, 3.0)) {
            it->setZValue(0);
        }
    }
    if (run.drawing) {
        const Drawing& stu = *run.drawing;
        // Wrong location counted once: the whole drawing is in the wrong place, so what matches
        // after moving it back is still "present but wrong" (yellow), not green.
        const QColor matched = r.wrongLocation() ? kShifted : kMatched;
        for (int j = 0; j < static_cast<int>(stu.prims.size()); ++j) {
            const PrimStatus st = r.stuStatus[j];
            QColor c = matched;
            if (st == PrimStatus::Extra) c = kExtra;
            else if (st == PrimStatus::NearMiss || st == PrimStatus::WrongAttribute) c = kShifted;
            else if (st == PrimStatus::NotCompared) c = QColor(120, 120, 200);
            if (auto* it = m_view->addPrimitive(stu.prims[j], c, 1.4, r.stuT[j])) it->setZValue(st == PrimStatus::Matched ? 1 : 3);
        }
    }
    if (const ViewSet* vs = m_batch->viewsOf(run); r.mode == Mode::Orthographic && vs)
        for (int v = 0; v < static_cast<int>(vs->views.size()); ++v) {
            const auto& vr = v < static_cast<int>(r.views.size()) ? r.views[v] : ViewResult{};
            m_view->addBox(vs->views[v].bbox, viewColor(v),
                           QStringLiteral("%1 %2").arg(vs->label(v), vr.found ? QStringLiteral("%1%").arg(vr.percent, 0, 'f', 0)
                                                                              : tr("not found")));
        }
    m_view->zoomToAll();
}

BBox DetailDialog::errorBox(int k) const {
    const StudentRun& run = m_batch->students[m_student];
    const CompareResult& r = run.result;
    BBox b;
    if (k < 0 || k >= static_cast<int>(r.errors.size())) return b;
    const ErrorItem& e = r.errors[k];
    for (int i : e.refPrims) b.add(m_batch->refDrawing(run).prims[i].bbox());
    if (run.drawing)
        for (int j : e.stuPrims)
            if (j >= 0 && j < static_cast<int>(run.drawing->prims.size())) b.add(transformed(run.drawing->prims[j], r.stuT[j]).bbox());
    if (const ViewSet* vs = m_batch->viewsOf(run); !b.valid() && e.view >= 0 && vs) b = vs->views[e.view].bbox;
    return b;
}

void DetailDialog::zoomToError(int k) {
    if (m_errors->currentRow() != k) {
        const QSignalBlocker block(m_errors);
        m_errors->setCurrentRow(k);
    }
    const BBox b = errorBox(k);
    if (!b.valid()) { m_view->zoomToAll(); return; }
    // Keep some context: at least 15% of the reference size around the error.
    const double minSize = 0.15 * m_batch->refDrawing(m_batch->students[m_student]).bbox().diagonal();
    BBox z = b;
    const Vec2 c = b.center();
    if (z.width() < minSize) { z.x0 = c.x - minSize / 2; z.x1 = c.x + minSize / 2; }
    if (z.height() < minSize) { z.y0 = c.y - minSize / 2; z.y1 = c.y + minSize / 2; }
    m_view->zoomTo(z, 0.2);
}

namespace {
QString statusText(PrimStatus s) {
    switch (s) {
    case PrimStatus::Matched: return QObject::tr("matched");
    case PrimStatus::Missing: return QObject::tr("missing");
    case PrimStatus::Extra: return QObject::tr("extra");
    case PrimStatus::NearMiss: return QObject::tr("near miss");
    case PrimStatus::WrongAttribute: return QObject::tr("wrong linetype/layer/pattern");
    case PrimStatus::NotCompared: return QObject::tr("not compared");
    }
    return {};
}
QColor statusColor(PrimStatus s) {
    switch (s) {
    case PrimStatus::Matched: return QColor(214, 245, 214);
    case PrimStatus::Missing: return QColor(250, 218, 214);
    case PrimStatus::NearMiss:
    case PrimStatus::WrongAttribute: return QColor(255, 243, 190);
    case PrimStatus::Extra: return QColor(234, 220, 250);
    case PrimStatus::NotCompared: return QColor(240, 240, 240);
    }
    return {};
}
}  // namespace

QWidget* DetailDialog::buildPrimitivesPage(QWidget* parent) {
    auto* page = new QWidget(parent);
    auto* lay = new QVBoxLayout(page);
    auto* top = new QHBoxLayout;
    m_primSource = new QComboBox(page);
    m_primSource->addItem(tr("Student drawing"));
    m_primSource->addItem(m_batch->multipleRefs()
                              ? tr("Solution drawing (%1)").arg(QFileInfo(m_batch->refPaths.value(m_batch->refOf(m_batch->students[m_student]))).fileName())
                              : tr("Reference drawing"));
    top->addWidget(new QLabel(tr("Show:"), page));
    top->addWidget(m_primSource);
    top->addStretch();
    lay->addLayout(top);
    auto* hint = new QLabel(tr("Everything the checker extracted from the file, in the file's own coordinates. "
                               "Click a row to zoom to it."),
                            page);
    hint->setWordWrap(true);
    hint->setForegroundRole(QPalette::PlaceholderText);
    lay->addWidget(hint);
    m_prims = new QTableWidget(page);
    m_prims->setColumnCount(7);
    m_prims->setHorizontalHeaderLabels({tr("#"), tr("Kind"), tr("Geometry"), tr("Layer"), tr("Linetype"), tr("Status"), tr("Source")});
    m_prims->verticalHeader()->setVisible(false);
    m_prims->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_prims->setSelectionMode(QAbstractItemView::SingleSelection);
    m_prims->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_prims->setWordWrap(false);
    lay->addWidget(m_prims, 1);
    m_primWarnings = new QLabel(page);
    m_primWarnings->setWordWrap(true);
    m_primWarnings->setTextInteractionFlags(Qt::TextSelectableByMouse);
    lay->addWidget(m_primWarnings);
    connect(m_primSource, &QComboBox::currentIndexChanged, this, &DetailDialog::fillPrimitives);
    connect(m_prims, &QTableWidget::currentCellChanged, this, [this](int row, int, int, int) { zoomToPrimitive(row); });
    fillPrimitives();
    return page;
}

void DetailDialog::fillPrimitives() {
    const StudentRun& run = m_batch->students[m_student];
    const bool student = m_primSource->currentIndex() == 0;
    const Drawing* d = student ? run.drawing.get() : &m_batch->refDrawing(run);
    const QSignalBlocker block(m_prims);
    m_prims->setRowCount(0);
    if (!d) return;
    const CompareResult& r = run.result;
    m_prims->setRowCount(static_cast<int>(d->prims.size()));
    for (int i = 0; i < static_cast<int>(d->prims.size()); ++i) {
        const Primitive& p = d->prims[i];
        const PrimStatus st = student ? r.stuStatus[i] : r.refStatus[i];
        QString lt = p.kind == PrimKind::Hatch ? QString() : QStringLiteral("%1 (%2)").arg(p.ltypeName, ltClassName(p.ltClass));
        QString src = p.sourceType + QStringLiteral("  ") + p.sourceId;
        if (!p.mergedFrom.isEmpty()) src += tr("  merged with %1").arg(p.mergedFrom.join(", "));
        if (p.stacked > 0) src += tr("  (%1 stacked duplicate(s) removed)").arg(p.stacked);
        const QStringList cells = {QString::number(i + 1), kindName(p.kind), describeGeometry(p), p.layer, lt, statusText(st), src};
        for (int c = 0; c < cells.size(); ++c) {
            auto* item = new QTableWidgetItem(cells[c]);
            if (c == 5) {
                item->setBackground(statusColor(st));
                item->setForeground(kDarkText);  // light background: dark text, also in dark mode
            }
            item->setToolTip(cells[c]);
            m_prims->setItem(i, c, item);
        }
    }
    m_prims->resizeColumnsToContents();
    QStringList w;
    for (const auto& dw : d->warnings) w << dw.text.toHtmlEscaped();
    m_primWarnings->setText(w.isEmpty() ? tr("No warnings for this file.") : tr("<b>Warnings:</b> %1").arg(w.join("; ")));
}

void DetailDialog::zoomToPrimitive(int row) {
    const StudentRun& run = m_batch->students[m_student];
    const bool student = m_primSource->currentIndex() == 0;
    const Drawing* d = student ? run.drawing.get() : &m_batch->refDrawing(run);
    if (!d || row < 0 || row >= static_cast<int>(d->prims.size())) return;
    // The overlay is in the reference frame: move student primitives there first.
    const Primitive p = student ? transformed(d->prims[row], run.result.stuT[row]) : d->prims[row];
    BBox z = p.bbox();
    const double minSize = 0.15 * m_batch->refDrawing(run).bbox().diagonal();
    const Vec2 c = z.center();
    if (z.width() < minSize) { z.x0 = c.x - minSize / 2; z.x1 = c.x + minSize / 2; }
    if (z.height() < minSize) { z.y0 = c.y - minSize / 2; z.y1 = c.y + minSize / 2; }
    m_view->zoomTo(z, 0.2);
}

void DetailDialog::buildMetadata(QTableWidget* t) {
    const StudentRun& run = m_batch->students[m_student];
    const DrawingMeta& a = m_batch->refDrawing(run).meta;
    const DrawingMeta b = run.drawing ? run.drawing->meta : DrawingMeta{};
    struct Row { QString name, ra, sb; bool identity; };
    auto secs = [](double s) { return s < 0 ? QStringLiteral("-") : QStringLiteral("%1:%2").arg(int(s) / 60).arg(int(s) % 60, 2, 10, QChar('0')); };
    const std::vector<Row> rows = {
        {tr("File"), a.fileName, b.fileName, false},
        {tr("SHA-256"), QString(a.sha256), QString(b.sha256), true},
        {tr("Size"), QString::number(a.size), QString::number(b.size), false},
        {tr("DWG version"), a.dwgVersion + " " + a.dwgVersionName, b.dwgVersion + " " + b.dwgVersionName, false},
        {tr("TDCREATE"), a.tdcreate.toString(), b.tdcreate.toString(), false},
        {tr("TDUCREATE (UTC)"), a.tducreate.toString(), b.tducreate.toString(), true},
        {tr("TDUPDATE"), a.tdupdate.toString(), b.tdupdate.toString(), false},
        {tr("TDUUPDATE (UTC)"), a.tduupdate.toString(), b.tduupdate.toString(), false},
        {tr("TDINDWG (editing time)"), secs(a.tdindwgSeconds), secs(b.tdindwgSeconds), false},
        {tr("FINGERPRINTGUID"), a.fingerprintGuid, b.fingerprintGuid, true},
        {tr("VERSIONGUID"), a.versionGuid, b.versionGuid, true},
        {tr("HANDSEED"), QString::number(a.handseed, 16).toUpper(), QString::number(b.handseed, 16).toUpper(), false},
        {tr("Author"), a.author, b.author, true},
        {tr("LastSavedBy"), a.lastSavedBy, b.lastSavedBy, true},
        {tr("RevisionNumber"), a.revisionNumber, b.revisionNumber, false},
        {tr("Saved by application"), a.appName + " " + a.appBuildVersion, b.appName + " " + b.appBuildVersion, false},
        {tr("INSUNITS"), insUnitsName(a.insUnits), insUnitsName(b.insUnits), false},
        {tr("Entities"), QString::number(a.entityCount), QString::number(b.entityCount), false},
        {tr("Parser"), a.parser, b.parser, false},
    };
    t->setColumnCount(2);
    t->setHorizontalHeaderLabels({tr("Reference"), tr("Student")});
    t->setRowCount(static_cast<int>(rows.size()));
    for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
        t->setVerticalHeaderItem(i, new QTableWidgetItem(rows[i].name));
        auto* ia = new QTableWidgetItem(rows[i].ra);
        auto* ib = new QTableWidgetItem(rows[i].sb);
        if (rows[i].identity && !rows[i].ra.isEmpty() && rows[i].ra == rows[i].sb) {
            ia->setBackground(QColor(255, 225, 190));
            ib->setBackground(QColor(255, 225, 190));
            ia->setForeground(kDarkText);
            ib->setForeground(kDarkText);
            ib->setToolTip(tr("Identical to the reference"));
        }
        t->setItem(i, 0, ia);
        t->setItem(i, 1, ib);
    }
    t->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
}

void DetailDialog::buildViews(QTableWidget* t) {
    const CompareResult& r = m_batch->students[m_student].result;
    t->setColumnCount(6);
    t->setHorizontalHeaderLabels({tr("View"), tr("Result"), tr("Matched"), tr("Extras"), tr("Translation"), tr("Checks")});
    t->setRowCount(static_cast<int>(r.views.size()));
    for (int i = 0; i < static_cast<int>(r.views.size()); ++i) {
        const ViewResult& v = r.views[i];
        QStringList checks;
        for (const auto& c : r.checks)
            if (c.view == v.view && c.applicable)
                checks << (c.passed ? (c.isArrangement ? tr("arrangement ok") : tr("aligned")) : c.text);
        auto* res = new QTableWidgetItem(!v.found ? tr("not found") : (v.pass ? tr("pass") : tr("fail")) +
                                                                          QStringLiteral(" %1%").arg(v.percent, 0, 'f', 1));
        res->setBackground(v.found && v.pass ? QColor(214, 245, 214) : QColor(250, 218, 214));
        res->setForeground(kDarkText);
        t->setItem(i, 0, new QTableWidgetItem(v.label));
        t->setItem(i, 1, res);
        t->setItem(i, 2, new QTableWidgetItem(QStringLiteral("%1 / %2").arg(v.matched).arg(v.refCount)));
        t->setItem(i, 3, new QTableWidgetItem(QString::number(v.extras)));
        t->setItem(i, 4, new QTableWidgetItem(v.found ? fmtPt({v.T.tx, v.T.ty}) : QString()));
        t->setItem(i, 5, new QTableWidgetItem(checks.join("; ")));
    }
    t->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Stretch);
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->setWordWrap(true);
    t->resizeRowsToContents();
}

}  // namespace dcgui
