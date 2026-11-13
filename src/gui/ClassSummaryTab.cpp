#include "ClassSummaryTab.h"

#include "DrawingView.h"
#include "GuiUtil.h"
#include "ResultsModel.h"

#include <QButtonGroup>
#include <QFileInfo>
#include <QGraphicsItem>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QSplitter>
#include <QStackedWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>

using namespace dc;

namespace dcgui {

namespace {

QString pct(double v) { return QStringLiteral("%1%").arg(v, 0, 'f', 1); }

// The drawing's colors (D90): green where every student got a part right, yellow for the
// most commonly missed parts, red for parts most of the class missed. Dark enough for thin
// lines on the light background.
const QColor kRight(30, 140, 60), kCommon(225, 175, 0), kMost(210, 30, 30), kNotGraded(175, 175, 175);

QColor mix(const QColor& a, const QColor& b, double u) {
    auto m = [u](int x, int y) { return static_cast<int>(x + (y - x) * u + 0.5); };
    return QColor(m(a.red(), b.red()), m(a.green(), b.green()), m(a.blue(), b.blue()));
}

// Green for none, toward yellow up to the most common share, red past half (missLevel).
QColor missColor(double rate, double common) {
    const double level = missLevel(rate, common);
    return level > 1 ? kMost : mix(kRight, kCommon, level);
}

// A table row in a light tint of its color.
void tint(QTableWidget* t, int row, const QColor& c) { tintRow(t, row, mix(c, Qt::white, 0.72)); }

QString swatch(const QColor& c) { return QStringLiteral("<span style='color:%1'>&#9632;</span>").arg(c.name()); }

QTableWidget* makeTable(int columns, QWidget* parent) {
    auto* t = new QTableWidget(parent);
    t->setColumnCount(columns);
    t->setSelectionBehavior(QAbstractItemView::SelectRows);
    t->setSelectionMode(QAbstractItemView::SingleSelection);
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->verticalHeader()->setVisible(false);
    t->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    t->horizontalHeader()->setStretchLastSection(true);
    t->setWordWrap(true);
    return t;
}

QWidget* titled(QLabel* title, QWidget* body, QWidget* parent) {
    auto* w = new QWidget(parent);
    auto* l = new QVBoxLayout(w);
    l->setContentsMargins(0, 0, 0, 0);
    l->addWidget(title);
    l->addWidget(body, 1);  // reparents both
    return w;
}

QTableWidgetItem* cell(const QString& text, Qt::Alignment align = Qt::AlignLeft | Qt::AlignVCenter) {
    auto* it = new QTableWidgetItem(text);
    it->setTextAlignment(align);
    return it;
}

}  // namespace

ScoreChart::ScoreChart(QWidget* parent) : QWidget(parent) { setMinimumHeight(110); }

void ScoreChart::setScores(const std::vector<double>& scores) {
    std::fill(std::begin(m_bins), std::end(m_bins), 0);
    for (const double v : scores) ++m_bins[std::clamp(static_cast<int>(v / 10.0), 0, 9)];  // 100 % goes with 90–100
    update();
}

void ScoreChart::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QFontMetrics fm = fontMetrics();
    const int top = fm.height() + 2, bottom = height() - fm.height() - 4;
    const int most = std::max(1, *std::max_element(std::begin(m_bins), std::end(m_bins)));
    const double slot = width() / 10.0;
    for (int k = 0; k < 10; ++k) {
        const double x = k * slot + 3, w = slot - 6;
        const double h = (bottom - top) * static_cast<double>(m_bins[k]) / most;
        const QRectF bar(x, bottom - h, w, h);
        p.fillRect(bar, scoreColor(k * 10 + 5));
        p.setPen(palette().color(QPalette::Mid));
        p.drawRect(bar);
        p.setPen(palette().color(QPalette::WindowText));
        if (m_bins[k] > 0)
            p.drawText(QRectF(x, bottom - h - fm.height() - 1, w, fm.height()), Qt::AlignCenter, QString::number(m_bins[k]));
    }
    // A label under each band when they fit; when the chart is narrow, 0 %, 20 %, … 100 % at
    // the band edges instead, so the labels never run into each other (D92).
    const QRectF row(0, bottom + 2, width(), fm.height());
    if (fm.horizontalAdvance(QStringLiteral("90–100%")) + 6 <= slot) {
        for (int k = 0; k < 10; ++k)
            p.drawText(QRectF(k * slot, row.top(), slot, row.height()), Qt::AlignCenter, QStringLiteral("%1–%2%").arg(k * 10).arg(k * 10 + 10));
    } else {
        for (int k = 0; k <= 10; k += 2) {
            const QString t = QStringLiteral("%1%").arg(k * 10);
            const double tw = fm.horizontalAdvance(t);
            const double x = std::clamp(k * slot - tw / 2, 0.0, width() - tw);
            p.drawText(QRectF(x, row.top(), tw + 1, row.height()), Qt::AlignLeft | Qt::AlignVCenter, t);
        }
    }
    p.setPen(palette().color(QPalette::Mid));
    p.drawLine(QPointF(0, bottom + 0.5), QPointF(width(), bottom + 0.5));
}

ClassSummaryTab::ClassSummaryTab(QWidget* parent) : QWidget(parent) {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    m_stack = new QStackedWidget(this);
    outer->addWidget(m_stack);
    auto* empty = new QLabel(tr("Run a batch to see how the class did: the scores, and the errors by type and by location "
                                "on the solution."),
                             m_stack);
    empty->setAlignment(Qt::AlignCenter);
    empty->setWordWrap(true);
    m_stack->addWidget(empty);

    auto* page = new QWidget(m_stack);
    auto* lay = new QVBoxLayout(page);
    // Several drawings: all of them first, then the choice of one, then that one (D90).
    m_overall = new QLabel(page);
    m_overall->setWordWrap(true);
    m_overall->setTextFormat(Qt::RichText);
    lay->addWidget(m_overall);
    m_drawingRow = new QWidget(page);
    m_drawingButtons = new QHBoxLayout(m_drawingRow);
    m_drawingButtons->setContentsMargins(0, 4, 0, 4);
    auto* choose = new QLabel(tr("<b>Solution drawing:</b>"), m_drawingRow);
    m_drawingButtons->addWidget(choose);
    m_drawingButtons->addStretch();
    m_drawingGroup = new QButtonGroup(this);
    m_drawingGroup->setExclusive(true);
    lay->addWidget(m_drawingRow);
    m_stats = new QLabel(page);
    m_stats->setWordWrap(true);
    m_stats->setTextFormat(Qt::RichText);
    lay->addWidget(m_stats);
    m_chart = new ScoreChart(page);
    lay->addWidget(m_chart);

    // The errors by type and by location, side by side in structure: what, how many students,
    // then the total or the types.
    auto* split = new QSplitter(Qt::Horizontal, page);
    lay->addWidget(split, 1);
    auto* tables = new QSplitter(Qt::Vertical, split);
    m_kinds = makeTable(3, page);
    m_parts = makeTable(3, page);
    m_kindsTitle = new QLabel(page);
    m_partsTitle = new QLabel(page);
    m_kindsTitle->setText(tr("<b>Errors by type</b> &nbsp;(click one to color the drawing by it)"));
    m_partsTitle->setText(tr("<b>Errors by location</b> &nbsp;(click one to zoom to it)"));
    tables->addWidget(titled(m_kindsTitle, m_kinds, tables));
    tables->addWidget(titled(m_partsTitle, m_parts, tables));
    auto* right = new QWidget(split);
    auto* rl = new QVBoxLayout(right);
    rl->setContentsMargins(0, 0, 0, 0);
    m_shadeNote = new QLabel(right);
    m_shadeNote->setWordWrap(true);
    rl->addWidget(m_shadeNote);
    m_view = new DrawingView(right);
    rl->addWidget(m_view, 1);
    auto* under = new QHBoxLayout;
    auto* legend = new QLabel(QStringLiteral("%1 %2 &nbsp; %3 %4 &nbsp; %5 %6 &nbsp; %7 %8")
                                  .arg(swatch(kRight), tr("got right by every student"), swatch(kCommon), tr("most commonly missed"),
                                       swatch(kMost), tr("missed by most"), swatch(kNotGraded), tr("not graded")),
                              right);
    legend->setTextFormat(Qt::RichText);
    legend->setWordWrap(true);
    auto* zoomAll = new QPushButton(tr("Zoom to all"), right);
    under->addWidget(legend, 1);
    under->addWidget(zoomAll);
    rl->addLayout(under);
    split->addWidget(tables);
    split->addWidget(right);
    split->setStretchFactor(0, 1);
    split->setStretchFactor(1, 1);
    m_stack->addWidget(page);

    connect(zoomAll, &QPushButton::clicked, m_view, &DrawingView::zoomToAll);
    connect(m_drawingGroup, &QButtonGroup::idClicked, this, [this](int ref) { showDrawing(ref); });
    connect(m_kinds, &QTableWidget::currentCellChanged, this, [this](int row, int, int, int) {
        const QTableWidgetItem* it = row >= 0 ? m_kinds->item(row, 0) : nullptr;
        m_kind = it ? it->data(Qt::UserRole).toString() : QString();
        shade();
    });
    connect(m_parts, &QTableWidget::cellClicked, this, [this](int row, int) {
        const QTableWidgetItem* it = m_parts->item(row, 0);
        if (!it || !m_batch) return;
        const Drawing& d = *m_batch->refs[m_sum.ref];
        const int prim = it->data(Qt::UserRole).toInt();
        if (prim < 0 || prim >= static_cast<int>(d.prims.size())) return;
        // Around the part, never smaller than a twentieth of the drawing (a dimension is a point).
        BBox all;
        for (const Primitive& p : d.prims) all.add(p.bbox());
        BBox box = d.prims[prim].bbox();
        const double least = 0.05 * std::max(all.diagonal(), 1e-9);
        if (box.diagonal() < least) {
            const Vec2 c = box.center();
            box = BBox();
            box.add(c - Vec2{least, least} * 0.5);
            box.add(c + Vec2{least, least} * 0.5);
        }
        m_view->zoomTo(box, 0.5);
    });
}

void ClassSummaryTab::setBatch(std::shared_ptr<const BatchOutput> b) {
    m_batch = std::move(b);
    m_kind.clear();
    for (QAbstractButton* old : m_drawingGroup->buttons()) {
        m_drawingGroup->removeButton(old);
        delete old;
    }
    if (!m_batch || m_batch->refs.empty() || m_batch->groups.empty()) {
        m_stack->setCurrentIndex(0);
        return;
    }
    m_stack->setCurrentIndex(1);
    const bool several = m_batch->multipleRefs();
    m_drawingRow->setVisible(several);
    m_overall->setVisible(several);
    if (several) {
        // A button per drawing, plain to see (D90), before the stretch at the row's end.
        for (int k = 0; k < static_cast<int>(m_batch->refPaths.size()); ++k) {
            auto* btn = new QPushButton(tr("Drawing %1: %2").arg(QString::number(k + 1), QFileInfo(m_batch->refPaths[k]).fileName()),
                                        m_drawingRow);
            btn->setCheckable(true);
            btn->setMinimumHeight(30);
            QFont f = btn->font();
            f.setPointSizeF(f.pointSizeF() * 1.1);
            btn->setFont(f);
            m_drawingGroup->addButton(btn, k);
            m_drawingButtons->insertWidget(m_drawingButtons->count() - 1, btn);
        }
        // All the drawings: each student's overall score (a drawing with no file counts 0).
        const ClassOverall o = summarizeClass(*m_batch);
        QStringList figures;
        figures << tr("<b>%1</b> students").arg(o.students) << tr("%1 handed in every drawing").arg(o.complete)
                << tr("overall score: mean <b>%1</b>").arg(pct(o.mean)) << tr("median %1").arg(pct(o.median));
        if (!o.scores.empty()) figures << tr("lowest %1").arg(pct(o.scores.front())) << tr("highest %1").arg(pct(o.scores.back()));
        m_overall->setText(tr("<b>All %1 drawings</b> (a student's overall score is the mean over the drawings, a missing one "
                              "counting 0):<br>%2")
                               .arg(QString::number(m_batch->refs.size()), figures.join(QStringLiteral(" &nbsp;·&nbsp; "))));
        m_drawingGroup->button(0)->setChecked(true);
    }
    showDrawing(0);
}

void ClassSummaryTab::showDrawing(int ref) {
    if (!m_batch || ref < 0 || ref >= static_cast<int>(m_batch->refs.size())) return;
    m_sum = summarizeDrawing(*m_batch, ref);
    m_kind.clear();
    const int n = m_sum.graded;
    // Each part described once, for the table and every recoloring's tooltips (D92).
    m_partText.clear();
    for (int i = 0; i < static_cast<int>(m_batch->refs[ref]->prims.size()); ++i) m_partText.push_back(describePart(*m_batch, ref, i));

    // The chosen drawing's figures.
    QStringList figures;
    figures << tr("<b>%1</b> graded").arg(n);
    if (m_sum.noFile > 0) figures << tr("%1 with no readable file").arg(m_sum.noFile);
    if (!m_sum.scores.empty())
        figures << tr("mean <b>%1</b>").arg(pct(m_sum.mean)) << tr("median %1").arg(pct(m_sum.median))
                << tr("lowest %1").arg(pct(m_sum.scores.front())) << tr("highest %1").arg(pct(m_sum.scores.back()));
    figures << tr("%1 with errors").arg(m_sum.withErrors);
    const QString heading = m_batch->multipleRefs()
                                ? tr("<b>Drawing %1</b> (%2): ").arg(QString::number(ref + 1), QFileInfo(m_batch->refPaths[ref]).fileName().toHtmlEscaped())
                                : QString();
    m_stats->setText(heading + figures.join(QStringLiteral(" &nbsp;·&nbsp; ")));
    m_chart->setScores(m_sum.scores);

    // Errors by type: all errors first (bold, as the total), then each type, tinted by how many
    // students had it.
    {
        const QSignalBlocker block(m_kinds);
        m_kinds->setRowCount(0);
        m_kinds->setHorizontalHeaderLabels({tr("Type of error"), tr("Number of students (out of %1)").arg(n), tr("Total number of errors")});
        std::vector<double> rates;
        for (const ErrorKindCount& k : m_sum.kinds) rates.push_back(n > 0 ? static_cast<double>(k.students) / n : 0);
        const double common = commonMissRate(rates);
        auto addRow = [this](const QString& label, const QString& key, int students, int times) {
            const int row = m_kinds->rowCount();
            m_kinds->insertRow(row);
            QTableWidgetItem* name = cell(label);
            name->setData(Qt::UserRole, key);
            m_kinds->setItem(row, 0, name);
            m_kinds->setItem(row, 1, cell(QString::number(students), Qt::AlignRight | Qt::AlignVCenter));
            m_kinds->setItem(row, 2, cell(QString::number(times), Qt::AlignRight | Qt::AlignVCenter));
            return row;
        };
        const int all = addRow(tr("all errors"), QString(), m_sum.withErrors, m_sum.errorCount);
        for (int c = 0; c < 3; ++c) {
            QFont f = m_kinds->item(all, c)->font();
            f.setBold(true);
            m_kinds->item(all, c)->setFont(f);
        }
        for (size_t k = 0; k < m_sum.kinds.size(); ++k) {
            const ErrorKindCount& e = m_sum.kinds[k];
            tint(m_kinds, addRow(e.label, e.label, e.students, e.times), missColor(rates[k], common));
        }
        m_kinds->resizeColumnsToContents();
        m_kinds->setCurrentCell(0, 0);
    }
    // Errors by location: the parts at least one student missed, most first, tinted as drawn.
    {
        m_parts->setRowCount(0);
        m_parts->setHorizontalHeaderLabels({tr("Location (part of the solution)"), tr("Number of students (out of %1)").arg(n),
                                            tr("Types of error")});
        std::vector<double> rates;
        for (const SolutionPartCount& p : m_sum.parts) rates.push_back(p.rate());
        const double common = commonMissRate(rates);
        for (const SolutionPartCount& p : m_sum.parts) {
            if (p.missed == 0) break;  // sorted: the rest were right everywhere
            const int row = m_parts->rowCount();
            m_parts->insertRow(row);
            QTableWidgetItem* name = cell(m_partText.at(p.prim));
            name->setData(Qt::UserRole, p.prim);
            m_parts->setItem(row, 0, name);
            // Out of N, unless some were not graded on this part (an unfound view): then its own count.
            m_parts->setItem(row, 1, cell(p.compared == n ? QString::number(p.missed) : tr("%1 (of %2)").arg(p.missed).arg(p.compared),
                                          Qt::AlignRight | Qt::AlignVCenter));
            m_parts->setItem(row, 2, cell(describeHow(p)));
            tint(m_parts, row, missColor(p.rate(), common));
        }
        m_parts->resizeColumnsToContents();
    }
    shade();
    m_view->zoomToAll();
}

void ClassSummaryTab::shade() {
    if (!m_batch || m_sum.ref < 0 || m_sum.ref >= static_cast<int>(m_batch->refs.size())) return;
    const Drawing& d = *m_batch->refs[m_sum.ref];
    const auto kind = std::find_if(m_sum.kinds.cbegin(), m_sum.kinds.cend(), [&](const ErrorKindCount& k) { return k.label == m_kind; });
    const bool byKind = !m_kind.isEmpty() && kind != m_sum.kinds.cend() && kind->onSolution;
    auto countOf = [&](const SolutionPartCount& p) { return byKind ? p.count(m_kind) : p.missed; };
    std::vector<double> rates;
    for (const SolutionPartCount& p : m_sum.parts) rates.push_back(p.compared > 0 ? static_cast<double>(countOf(p)) / p.compared : 0);
    const double common = commonMissRate(rates);
    const bool anyMissed = std::any_of(rates.cbegin(), rates.cend(), [](double r) { return r > 0; });
    const QString scale = anyMissed ? tr("Yellow: the most commonly missed (%1 of the students); red: more than half.").arg(pct(100 * common))
                                    : tr("No student missed any of it.");
    if (m_kind.isEmpty())
        m_shadeNote->setText(tr("Each part of the solution in color by how many students missed it or got it wrong. ") + scale);
    else if (byKind)
        m_shadeNote->setText(tr("Each part of the solution in color by how many students had \"%1\" on it. ").arg(m_kind) + scale);
    else
        m_shadeNote->setText(tr("\"%1\" is about the students' own drawings, not a part of the solution: the drawing shows "
                                "all errors.").arg(m_kind));
    m_view->clearDrawing();
    for (int i = 0; i < static_cast<int>(d.prims.size()); ++i) {
        const int k = i < static_cast<int>(m_sum.partOf.size()) ? m_sum.partOf[i] : -1;
        QColor color = kNotGraded;
        QString tip = (i < static_cast<int>(m_partText.size()) ? m_partText[i] : QString()) + QLatin1Char('\n');
        if (k < 0) {
            tip += tr("not graded");
        } else {
            const SolutionPartCount& p = m_sum.parts[k];
            const double rate = rates[k];
            color = missColor(rate, common);
            tip += tr("%1 of %2 students (%3)").arg(countOf(p)).arg(p.compared).arg(pct(100 * rate)) +
                   (p.how.empty() ? QString() : QStringLiteral(": ") + describeHow(p));
        }
        if (QGraphicsItem* it = m_view->addPrimitive(d.prims[i], color, 2.0)) it->setToolTip(tip);
    }
}

}  // namespace dcgui
