#include "DuplicatesTab.h"

#include <QCheckBox>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QSplitter>
#include <QTableWidget>
#include <QTextBrowser>
#include <QVBoxLayout>

using namespace dc;

namespace dcgui {

namespace {
QColor levelColor(Strength s) {
    switch (s) {
    case Strength::Strong: return QColor(250, 210, 205);
    case Strength::Medium: return QColor(255, 232, 190);
    case Strength::Weak: return QColor(255, 245, 205);
    case Strength::Info: return QColor(240, 240, 240);
    }
    return {};
}
}  // namespace

DuplicatesTab::DuplicatesTab(QWidget* parent) : QWidget(parent) {
    auto* lay = new QVBoxLayout(this);
    m_notes = new QLabel(this);
    m_notes->setWordWrap(true);
    lay->addWidget(m_notes);
    auto* hint = new QLabel(tr("Signals are evidence for a closer look, not conclusions. Strong signals are listed first."), this);
    hint->setStyleSheet(QStringLiteral("color: #666"));
    lay->addWidget(hint);
    m_showInfo = new QCheckBox(tr("Show informational-only pairs"), this);
    lay->addWidget(m_showInfo);

    auto* split = new QSplitter(Qt::Horizontal, this);
    lay->addWidget(split, 1);
    auto* left = new QSplitter(Qt::Vertical, split);
    m_pairs = new QTableWidget(left);
    m_pairs->setColumnCount(5);
    m_pairs->setHorizontalHeaderLabels({tr("File A"), tr("File B"), tr("Level"), tr("Score"), tr("Signals")});
    m_pairs->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    m_pairs->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_pairs->setSelectionMode(QAbstractItemView::SingleSelection);
    m_pairs->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_pairs->setSortingEnabled(false);
    m_clusters = new QListWidget(left);
    left->addWidget(m_pairs);
    left->addWidget(m_clusters);
    left->setStretchFactor(0, 3);
    left->setStretchFactor(1, 1);
    m_details = new QTextBrowser(split);
    split->addWidget(left);
    split->addWidget(m_details);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 2);

    connect(m_showInfo, &QCheckBox::toggled, this, &DuplicatesTab::rebuild);
    connect(m_pairs, &QTableWidget::currentCellChanged, this, [this](int row, int, int, int) { showDetails(row); });
}

void DuplicatesTab::setBatch(std::shared_ptr<const BatchOutput> b) {
    m_batch = std::move(b);
    rebuild();
}

void DuplicatesTab::rebuild() {
    m_pairs->setRowCount(0);
    m_clusters->clear();
    m_rowToPair.clear();
    m_details->clear();
    if (!m_batch) return;
    const auto& d = m_batch->dups;
    // Each note escaped, then joined: escaping the joined text showed the <br> as text (D76).
    QStringList notes;
    for (const QString& n : d.notes) notes << n.toHtmlEscaped();
    m_notes->setText(notes.isEmpty() ? QString() : QStringLiteral("<b>%1</b> %2").arg(tr("Notes:"), notes.join(QStringLiteral("<br>"))));
    for (int k = 0; k < static_cast<int>(d.pairs.size()); ++k) {
        const PairResult& p = d.pairs[k];
        if (!p.flagged() && !m_showInfo->isChecked()) continue;
        const int row = m_pairs->rowCount();
        m_pairs->insertRow(row);
        QStringList labels;
        for (const auto& s : p.evidence) labels << s.label;
        const QList<QTableWidgetItem*> items = {
            new QTableWidgetItem(m_batch->dupEntries[p.a].display), new QTableWidgetItem(m_batch->dupEntries[p.b].display),
            new QTableWidgetItem(strengthName(p.level)), new QTableWidgetItem(QString::number(p.score, 'f', 0)),
            new QTableWidgetItem(labels.join(", "))};
        for (int c = 0; c < items.size(); ++c) {
            items[c]->setBackground(levelColor(p.level));
            items[c]->setForeground(QColor(20, 20, 20));
            m_pairs->setItem(row, c, items[c]);
        }
        m_rowToPair.push_back(k);
    }
    m_pairs->resizeColumnsToContents();
    m_pairs->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    int n = 1;
    for (const auto& c : d.clusters) {
        QStringList names;
        for (int i : c) names << m_batch->dupEntries[i].display;
        m_clusters->addItem(tr("Cluster %1 (%2 files, joined by strong signals): %3").arg(n++).arg(c.size()).arg(names.join(", ")));
    }
    if (d.clusters.empty()) m_clusters->addItem(tr("No clusters of files joined by strong signals."));
}

void DuplicatesTab::showPair(int pairIndex) {
    for (int row = 0; row < static_cast<int>(m_rowToPair.size()); ++row)
        if (m_rowToPair[row] == pairIndex) {
            m_pairs->setCurrentCell(row, 0);
            m_pairs->scrollToItem(m_pairs->item(row, 0));
            return;
        }
}

void DuplicatesTab::showDetails(int row) {
    if (!m_batch || row < 0 || row >= static_cast<int>(m_rowToPair.size())) { m_details->clear(); return; }
    const PairResult& p = m_batch->dups.pairs[m_rowToPair[row]];
    const DupEntry& a = m_batch->dupEntries[p.a];
    const DupEntry& b = m_batch->dupEntries[p.b];
    // One arg() call each: a later call would fill a "%1" inside a file or student name (D76).
    QString html = QStringLiteral("<h3>%1 &harr; %2</h3><p>Level: <b>%3</b>, combined score %4</p><table cellpadding=3>")
                       .arg(a.display.toHtmlEscaped(), b.display.toHtmlEscaped(), strengthName(p.level),
                            QString::number(p.score, 'f', 0));
    for (const auto& s : p.evidence)
        html += QStringLiteral("<tr><td><b>%1</b></td><td>%2</td><td>weight %3</td></tr><tr><td></td><td colspan=2>%4</td></tr>")
                    .arg(s.label.toHtmlEscaped(), strengthName(s.strength), QString::number(s.weight, 'f', 0),
                         s.detail.toHtmlEscaped());
    html += QStringLiteral("</table>");
    auto files = [](const DupEntry& e) { return e.drawing ? e.drawing->meta.path.toHtmlEscaped() : QString(); };
    html += QStringLiteral("<p style='color:#666'>%1<br>%2</p>").arg(files(a), files(b));
    m_details->setHtml(html);
}

}  // namespace dcgui
