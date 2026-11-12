#include "ViewEditorDialog.h"

#include "DrawingView.h"

#include <QComboBox>
#include <QFormLayout>
#include <QGraphicsItem>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QSplitter>
#include <QTableWidget>
#include <QVBoxLayout>

using namespace dc;

namespace dcgui {

QColor ViewEditorDialog::colorFor(int v) {
    static const QList<QColor> palette = {QColor(31, 119, 180), QColor(214, 39, 40), QColor(44, 160, 44), QColor(148, 103, 189),
                                          QColor(255, 127, 14), QColor(140, 86, 75), QColor(227, 119, 194), QColor(23, 190, 207)};
    if (v < 0) return QColor(160, 160, 160);
    return palette[v % palette.size()];
}

ViewEditorDialog::ViewEditorDialog(DrawingPtr ref, const QString& refPath, const ViewSet& initial, QWidget* parent)
    : QDialog(parent), m_ref(std::move(ref)), m_refPath(refPath), m_vs(initial) {
    setWindowTitle(tr("Reference views"));
    resize(1200, 760);
    auto* lay = new QVBoxLayout(this);
    auto* help = new QLabel(tr("Confirm how the reference splits into views. Drag with the left button to lasso primitives "
                               "(Shift adds to the selection), then assign them to a view. Right-drag pans, the wheel zooms. "
                               "Name a view \"Ignored\" to leave it out of grading, e.g. a title block or notes: it is "
                               "still found in each drawing, so a student's copy of it is not counted as extra."),
                            this);
    help->setWordWrap(true);
    lay->addWidget(help);
    auto* split = new QSplitter(Qt::Horizontal, this);
    lay->addWidget(split, 1);
    m_view = new DrawingView(split);
    m_view->setLassoEnabled(true);
    split->addWidget(m_view);

    auto* side = new QWidget(split);
    auto* sl = new QVBoxLayout(side);
    auto* form = new QFormLayout;
    m_gap = new QSlider(Qt::Horizontal, side);
    m_gap->setRange(1, 200);  // 0.05% .. 10% in 0.05% steps
    m_gap->setValue(std::clamp(static_cast<int>(std::lround(m_vs.gapFraction * 2000)), 1, 200));
    m_gapLabel = new QLabel(side);
    form->addRow(tr("Gap threshold"), m_gap);
    form->addRow(QString(), m_gapLabel);
    sl->addLayout(form);
    m_table = new QTableWidget(side);
    m_table->setColumnCount(3);
    m_table->setHorizontalHeaderLabels({tr("Color"), tr("Name"), tr("Primitives")});
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->verticalHeader()->setVisible(false);
    sl->addWidget(m_table, 1);
    auto* assignRow = new QHBoxLayout;
    m_target = new QComboBox(side);
    auto* assign = new QPushButton(tr("Assign selection"), side);
    assignRow->addWidget(m_target, 1);
    assignRow->addWidget(assign);
    sl->addLayout(assignRow);
    auto* newView = new QPushButton(tr("New view from selection"), side);
    sl->addWidget(newView);
    m_status = new QLabel(side);
    sl->addWidget(m_status);
    split->addWidget(side);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 1);

    auto* buttons = new QHBoxLayout;
    auto* resegmentBtn = new QPushButton(tr("Auto-segment again"), this);
    auto* saveBtn = new QPushButton(tr("Save views"), this);
    auto* cancel = new QPushButton(tr("Cancel"), this);
    saveBtn->setDefault(true);
    buttons->addWidget(resegmentBtn);
    buttons->addStretch();
    buttons->addWidget(saveBtn);
    buttons->addWidget(cancel);
    lay->addLayout(buttons);

    // The gap applies when the slider is let go (a drag no longer re-segments at every step), or
    // at once from the keyboard or the wheel (D76).
    connect(m_gap, &QSlider::valueChanged, this, [this](int v) {
        if (m_gap->isSliderDown()) m_gapLabel->setText(tr("%1 % of the drawing diagonal (let go to apply)").arg(v / 20.0, 0, 'f', 2));
        else resegment(true);
    });
    connect(m_gap, &QSlider::sliderReleased, this, [this]() { resegment(true); });
    connect(resegmentBtn, &QPushButton::clicked, this, [this]() { resegment(false); });
    connect(m_view, &DrawingView::lassoFinished, this, &ViewEditorDialog::onLasso);
    connect(assign, &QPushButton::clicked, this, &ViewEditorDialog::assignSelection);
    connect(newView, &QPushButton::clicked, this, &ViewEditorDialog::addView);
    connect(saveBtn, &QPushButton::clicked, this, &ViewEditorDialog::save);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);

    // One view per primitive of this drawing, so that a selection can always be assigned (D76).
    if (m_vs.primView.size() != m_ref->prims.size()) {
        m_vs.primView.resize(m_ref->prims.size(), -1);  // -1: rebuild() puts it in the nearest view
        m_vs.rebuild(*m_ref);
    }
    refreshTable();
    redraw();
    m_view->zoomToAll();
}

bool ViewEditorDialog::resegment(bool onlyIfGapChanged) {
    const double gap = m_gap->value() / 2000.0;
    const int inUse = std::clamp(static_cast<int>(std::lround(m_vs.gapFraction * 2000)), 1, 200);
    if (onlyIfGapChanged && m_gap->value() == inUse) {  // a click on the handle, let go in place
        refreshTable();  // the label shows the gap in use again
        return false;
    }
    if (m_edited &&
        QMessageBox::question(this, tr("Views"),
                              tr("Splitting the drawing again throws away the views you assigned or named. Continue?")) !=
            QMessageBox::Yes) {
        const QSignalBlocker block(m_gap);  // the slider goes back to the gap in use
        m_gap->setValue(inUse);
        refreshTable();
        return false;
    }
    m_vs = segmentViews(*m_ref, gap);
    m_selected.clear();
    m_edited = false;
    refreshTable();
    redraw();
    return true;
}

void ViewEditorDialog::refreshTable() {
    m_gapLabel->setText(tr("%1 % of the drawing diagonal").arg(m_vs.gapFraction * 100, 0, 'f', 2));
    m_table->setRowCount(static_cast<int>(m_vs.views.size()));
    m_target->clear();
    for (int v = 0; v < static_cast<int>(m_vs.views.size()); ++v) {
        auto* sw = new QTableWidgetItem;
        sw->setBackground(colorFor(v));
        sw->setFlags(Qt::ItemIsEnabled);
        m_table->setItem(v, 0, sw);
        auto* names = new QComboBox(m_table);
        for (ViewName n : allViewNames()) names->addItem(viewNameString(n), static_cast<int>(n));
        names->setCurrentIndex(names->findData(static_cast<int>(m_vs.views[v].name)));
        connect(names, &QComboBox::currentIndexChanged, this, [this, v, names](int) {
            m_vs.views[v].name = static_cast<ViewName>(names->currentData().toInt());
            m_edited = true;
            // Only labels change; rebuilding the table here would delete this combo box mid-signal.
            for (int k = 0; k < m_target->count(); ++k) m_target->setItemText(k, m_vs.label(m_target->itemData(k).toInt()));
            redraw();
        });
        m_table->setCellWidget(v, 1, names);
        auto* cnt = new QTableWidgetItem(QString::number(m_vs.views[v].prims.size()));
        cnt->setFlags(Qt::ItemIsEnabled);
        m_table->setItem(v, 2, cnt);
        m_target->addItem(m_vs.label(v), v);
    }
    m_status->setText(tr("%1 primitives selected").arg(m_selected.size()));
}

void ViewEditorDialog::redraw() {
    m_view->clearDrawing();
    for (int i = 0; i < static_cast<int>(m_ref->prims.size()); ++i) {
        const int v = i < static_cast<int>(m_vs.primView.size()) ? m_vs.primView[i] : -1;
        const bool sel = m_selected.count(i) > 0;
        auto* item = m_view->addPrimitive(m_ref->prims[i], sel ? QColor(0, 0, 0) : colorFor(v), sel ? 3.0 : 1.6);
        if (item) item->setZValue(sel ? 2 : 1);
    }
    for (int v = 0; v < static_cast<int>(m_vs.views.size()); ++v) m_view->addBox(m_vs.views[v].bbox, colorFor(v), m_vs.label(v));
}

void ViewEditorDialog::onLasso(const QPolygonF& poly, bool add) {
    if (!add) m_selected.clear();
    for (int i = 0; i < static_cast<int>(m_ref->prims.size()); ++i) {
        // A point on what is drawn: an arc's middle, not its center, which can lie far from the
        // arc, even in another view (D76).
        const Vec2 c = m_ref->prims[i].midpoint();
        if (poly.containsPoint(QPointF(c.x, c.y), Qt::OddEvenFill)) m_selected.insert(i);
    }
    refreshTable();
    redraw();
}

void ViewEditorDialog::assignSelection() {
    const int v = m_target->currentData().toInt();
    if (m_selected.empty() || v < 0 || v >= static_cast<int>(m_vs.views.size())) return;
    for (int i : m_selected)
        if (i < static_cast<int>(m_vs.primView.size())) m_vs.primView[i] = v;
    m_selected.clear();
    m_edited = true;
    m_vs.rebuild(*m_ref);
    refreshTable();
    redraw();
}

void ViewEditorDialog::addView() {
    if (m_selected.empty()) return;
    ViewDef d;
    d.id = static_cast<int>(m_vs.views.size());
    d.name = ViewName::Other;
    m_vs.views.push_back(d);
    for (int i : m_selected)
        if (i < static_cast<int>(m_vs.primView.size())) m_vs.primView[i] = d.id;
    m_selected.clear();
    m_edited = true;
    // Drop views that became empty and renumber.
    m_vs.rebuild(*m_ref);
    std::vector<int> remap(m_vs.views.size(), -1);
    std::vector<ViewDef> kept;
    for (size_t v = 0; v < m_vs.views.size(); ++v)
        if (!m_vs.views[v].prims.empty()) {
            remap[v] = static_cast<int>(kept.size());
            ViewDef k = m_vs.views[v];
            k.id = remap[v];
            kept.push_back(k);
        }
    for (int& pv : m_vs.primView) pv = pv >= 0 && pv < static_cast<int>(remap.size()) ? remap[pv] : -1;
    m_vs.views = kept;
    m_vs.rebuild(*m_ref);
    refreshTable();
    redraw();
}

void ViewEditorDialog::save() {
    int fronts = 0;
    for (const auto& v : m_vs.views) fronts += v.name == ViewName::Front;
    if (fronts != 1 &&
        QMessageBox::question(this, tr("Views"), tr("Alignment checks need exactly one Front view (found %1). Save anyway?").arg(fronts)) !=
            QMessageBox::Yes)
        return;
    m_vs.refSha256 = m_ref->meta.sha256;
    QString err;
    m_saved = saveSidecar(sidecarPath(m_refPath), m_vs, *m_ref, &err);
    if (m_saved) {
        m_vs.source = sidecarPath(m_refPath);
    } else {  // no sidecar to name as the source (D76)
        m_vs.source = QStringLiteral("editor");
        QMessageBox::warning(this, tr("Views"), tr("Could not save the sidecar: %1\nThe views are still used for this session.").arg(err));
    }
    accept();
}

}  // namespace dcgui
