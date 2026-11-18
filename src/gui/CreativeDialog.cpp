#include "CreativeDialog.h"

#include "DrawingView.h"

#include <QCheckBox>
#include <QFileInfo>
#include <QGraphicsItem>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QTableWidget>
#include <QVBoxLayout>

using namespace dc;

namespace dcgui {

CreativeDialog::CreativeDialog(DrawingPtr ref, const QString& refPath, const CompareSettings& s, const CreativeChoices& initial,
                               QWidget* parent)
    : QDialog(parent), m_ref(std::move(ref)), m_initial(initial) {
    setWindowTitle(tr("Choose items: %1").arg(QFileInfo(refPath).fileName()));
    resize(1200, 760);
    m_items = creativeItems(*m_ref, s);
    m_offerCenterlines = hasCenterlines(*m_ref);
    m_corners = countCorners(*m_ref, s.linTol);
    m_dark = QSettings().value(QStringLiteral("preview/darkBackground"), true).toBool();  // as the previews
    m_mergeLinetypes = !s.matchLinetype;
    m_mergePatterns = !s.checkHatchPattern;

    auto* lay = new QVBoxLayout(this);
    auto* help = new QLabel(tr("Creative mode: a student's drawing needs each checked kind of object at least as many times as "
                               "required, anywhere and at any size. Extra objects never count against the student. Where all "
                               "of the solution's objects of a kind are on one layer, or in one color or lineweight, a student's "
                               "must be too, when Settings asks for that. Select a kind to see its objects in the solution."),
                            this);
    help->setWordWrap(true);
    lay->addWidget(help);
    auto* split = new QSplitter(Qt::Horizontal, this);
    lay->addWidget(split, 1);
    m_view = new DrawingView(split);
    split->addWidget(m_view);

    auto* side = new QWidget(split);
    auto* sl = new QVBoxLayout(side);
    sl->setContentsMargins(0, 0, 0, 0);
    m_table = new QTableWidget(side);
    m_table->setColumnCount(3);
    m_table->setHorizontalHeaderLabels({tr("Kind"), tr("In the solution"), tr("Required")});
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    sl->addWidget(m_table, 1);
    m_total = new QLabel(side);
    m_total->setWordWrap(true);
    sl->addWidget(m_total);

    auto* rules = new QGroupBox(tr("Drafting rules"), side);
    auto* rl = new QVBoxLayout(rules);
    m_centerlines = new QCheckBox(rules);
    m_centerlines->setToolTip(tr("Each circle, and each arc of 180° or more (fillets are left alone), needs center-linetype lines "
                                 "through its center in two directions, each reaching past it on both sides. Center-linetype and "
                                 "phantom circles need none."));
    m_cornersMeet = new QCheckBox(rules);
    m_cornersMeet->setToolTip(tr("An end of a line, arc or spline that comes within 1% of the drawing's size of another object "
                                 "without touching it is a gap; a line that runs past another object by as little is an "
                                 "overshoot. Ends far from everything are not judged, nor are centerlines and phantom lines. "
                                 "A hidden line may stop short of the visible line it continues."));
    if (m_offerCenterlines) {
        m_centerlines->setText(tr("Every circle has centerlines"));
    } else {
        m_centerlines->setText(tr("Every circle has centerlines (not offered: the solution has no centerlines)"));
        m_centerlines->setEnabled(false);
    }
    if (m_corners > 0) {
        m_cornersMeet->setText(tr("Corners meet (the solution has %n)", "corners", m_corners));
    } else {
        m_cornersMeet->setText(tr("Corners meet (not offered: no objects in the solution meet end to end)"));
        m_cornersMeet->setEnabled(false);
    }
    rl->addWidget(m_centerlines);
    rl->addWidget(m_cornersMeet);
    sl->addWidget(rules);
    split->addWidget(side);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 2);

    auto* buttons = new QHBoxLayout;
    auto* defaults = new QPushButton(tr("Restore defaults"), this);
    defaults->setToolTip(tr("Every kind checked, as many as in the solution, and the rules on"));
    auto* ok = new QPushButton(tr("OK"), this);
    auto* cancel = new QPushButton(tr("Cancel"), this);
    ok->setDefault(true);
    buttons->addWidget(defaults);
    buttons->addStretch();
    buttons->addWidget(ok);
    buttons->addWidget(cancel);
    lay->addLayout(buttons);

    connect(defaults, &QPushButton::clicked, this, [this]() { fill(CreativeChoices{}); });
    connect(ok, &QPushButton::clicked, this, &QDialog::accept);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_table, &QTableWidget::itemSelectionChanged, this, &CreativeDialog::highlight);
    connect(m_table, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* it) {
        if (it->column() != 0) return;
        if (QWidget* spin = m_table->cellWidget(it->row(), 2)) spin->setEnabled(it->checkState() == Qt::Checked);
        updateTotal();
    });

    fill(initial);
    highlight();
    m_view->zoomToAll();
}

void CreativeDialog::fill(const CreativeChoices& c) {
    {
        const QSignalBlocker block(m_table);
        m_table->setRowCount(static_cast<int>(m_items.size()));
        for (int r = 0; r < static_cast<int>(m_items.size()); ++r) {
            const CreativeItems& it = m_items[r];
            const bool on = !c.off.contains(it.kind.key);
            auto* kind = new QTableWidgetItem(it.kind.plural);
            kind->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
            kind->setCheckState(on ? Qt::Checked : Qt::Unchecked);
            m_table->setItem(r, 0, kind);
            auto* have = new QTableWidgetItem(QString::number(it.count));
            have->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            have->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            m_table->setItem(r, 1, have);
            auto* spin = new QSpinBox(m_table);
            spin->setRange(1, 99999);
            const auto n = c.counts.find(it.kind.key);
            spin->setValue(n != c.counts.end() ? std::max(1, n->second) : it.count);
            spin->setToolTip(tr("How many a student's drawing needs, at least"));
            spin->setEnabled(on);
            m_table->setCellWidget(r, 2, spin);
            connect(spin, &QSpinBox::valueChanged, this, &CreativeDialog::updateTotal);
        }
    }
    m_centerlines->setChecked(m_offerCenterlines && c.centerlines);
    m_cornersMeet->setChecked(m_corners > 0 && c.corners);
    updateTotal();
}

int CreativeDialog::required(int row) const {
    const auto* spin = qobject_cast<const QSpinBox*>(m_table->cellWidget(row, 2));
    return spin ? spin->value() : 0;
}

void CreativeDialog::updateTotal() {
    if (m_items.empty()) {
        m_total->setText(tr("The solution has no objects to count."));
        return;
    }
    // As graded: the settings may count some of the kinds listed together (D108).
    const std::vector<CreativeNeed> needs = creativeNeeds(m_items, choices());
    int objects = 0;
    QStringList merged;
    for (const CreativeNeed& n : needs) {
        objects += n.need;
        if (n.merged) merged << countOf(n.need, n.key, n.plural);
    }
    QString text = needs.empty() ? tr("No kind is checked: only the rules are graded.")
                                 : tr("A student's drawing needs %1 objects of %2 kinds.").arg(objects).arg(needs.size());
    if (!merged.isEmpty()) {
        QStringList why;
        if (m_mergeLinetypes) why << tr("\"Linetypes must match\" is off");
        if (m_mergePatterns) why << tr("\"Hatch pattern must match\" is off");
        text += QLatin1Char(' ') + tr("%1 under Settings, so these count together, whatever their linetype or pattern: %2.")
                                       .arg(why.join(tr(" and ")), merged.join(QStringLiteral(", ")));
    }
    m_total->setText(text);
}

void CreativeDialog::highlight() {
    m_view->showAsDrawn(m_ref.get(), m_dark);  // keeps the zoom and pan
    const QModelIndexList rows = m_table->selectionModel()->selectedRows();
    if (rows.isEmpty()) return;
    const int r = rows.front().row();
    if (r < 0 || r >= static_cast<int>(m_items.size())) return;
    const QColor c = m_dark ? QColor(255, 200, 0) : QColor(0, 110, 230);
    for (int i : m_items[r].prims)
        if (QGraphicsItem* g = m_view->addPrimitive(m_ref->prims[i], c, 3.0)) g->setZValue(3e9);  // above everything as drawn
}

CreativeChoices CreativeDialog::choices() const {
    CreativeChoices c = m_initial;
    // The kinds listed are as chosen here; any others keep what they had.
    for (const CreativeItems& it : m_items) {
        c.off.removeAll(it.kind.key);
        c.counts.erase(it.kind.key);
    }
    for (int r = 0; r < static_cast<int>(m_items.size()); ++r) {
        const QString& key = m_items[r].kind.key;
        if (m_table->item(r, 0)->checkState() != Qt::Checked) c.off << key;
        const int n = required(r);
        if (n != m_items[r].count) c.counts[key] = n;
    }
    if (m_offerCenterlines) c.centerlines = m_centerlines->isChecked();
    if (m_corners > 0) c.corners = m_cornersMeet->isChecked();
    return c;
}

}  // namespace dcgui
