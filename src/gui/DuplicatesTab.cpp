#include "DuplicatesTab.h"

#include "GuiUtil.h"

#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
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

constexpr int kListed = 6;  // students or files listed in a cell before "…and N more"

// A list for a cell, one item per line: the first kListed, then "…and N more students".
QString listCell(const QStringList& items, const QString& one, const QString& many) {
    if (items.size() <= kListed) return items.join(QLatin1Char('\n'));
    const int more = static_cast<int>(items.size()) - kListed;
    return items.mid(0, kListed).join(QLatin1Char('\n')) + QLatin1Char('\n') +
           DuplicatesTab::tr("…and %1 more %2").arg(more).arg(more == 1 ? one : many);
}

}  // namespace

DuplicatesTab::DuplicatesTab(QWidget* parent) : QWidget(parent) {
    auto* lay = new QVBoxLayout(this);
    m_notes = new QLabel(this);
    m_notes->setWordWrap(true);
    lay->addWidget(m_notes);
    auto* hint = new QLabel(tr("Each row is a group of students linked by evidence, strongest first; a weaker link to someone "
                               "else gets a row of its own. Signals are evidence for a closer look, not conclusions."),
                            this);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color: #666"));
    lay->addWidget(hint);
    lay->addWidget(new QLabel(tr("See full details by clicking a row."), this));
    m_showInfo = new QCheckBox(tr("Show groups linked by informational signals only"), this);
    lay->addWidget(m_showInfo);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(5);
    m_table->setHorizontalHeaderLabels({tr("Students involved"), tr("Files involved"), tr("Level"), tr("Score"), tr("Signals")});
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSortingEnabled(false);  // the groups come strongest first
    m_table->setWordWrap(true);
    m_table->verticalHeader()->setVisible(false);
    // Rows are several lines tall: scrolling a whole row at a time jumps (D84).
    m_table->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_table->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    lay->addWidget(m_table, 1);

    connect(m_showInfo, &QCheckBox::toggled, this, &DuplicatesTab::rebuild);
    connect(m_table, &QTableWidget::cellClicked, this, [this](int row, int) { openDetails(row); });
    connect(m_table, &QTableWidget::cellActivated, this, [this](int row, int) { openDetails(row); });  // Enter
}

void DuplicatesTab::setBatch(std::shared_ptr<const BatchOutput> b) {
    m_batch = std::move(b);
    rebuild();
}

void DuplicatesTab::closeDetails() {
    for (auto& d : m_open)
        if (d) d->close();
    m_open.clear();
}

void DuplicatesTab::rebuild() {
    closeDetails();  // they are for rows that are about to change
    m_table->setRowCount(0);
    m_groups.clear();
    if (!m_batch) return;
    const auto& d = m_batch->dups;
    const auto& E = m_batch->dupEntries;
    // Each note escaped, then joined: escaping the joined text showed the <br> as text (D76).
    QStringList notes;
    for (const QString& n : d.notes) notes << n.toHtmlEscaped();
    m_notes->setText(notes.isEmpty() ? QString() : QStringLiteral("<b>%1</b> %2").arg(tr("Notes:"), notes.join(QStringLiteral("<br>"))));
    m_groups = groupEvidence(d, E, m_showInfo->isChecked());
    m_table->setRowCount(static_cast<int>(m_groups.size()));
    for (int row = 0; row < static_cast<int>(m_groups.size()); ++row) {
        const EvidenceGroup& g = m_groups[row];
        // Files by student and drawing, as the results name them; the file names on hover (D84).
        QStringList files, names, signalLines;
        for (const int i : g.files) {
            files << evidenceFileLabel(E[i]);
            names << evidenceFileName(E[i]);
        }
        for (const auto& l : g.lines) signalLines << l.text;
        QString students = listCell(g.students, tr("student"), tr("students"));
        if (!g.with.isEmpty()) students += QLatin1Char('\n') + tr("with %1").arg(g.with);
        const QList<QTableWidgetItem*> items = {new QTableWidgetItem(students), new QTableWidgetItem(listCell(files, tr("file"), tr("files"))),
                                                new QTableWidgetItem(strengthName(g.level)),
                                                new QTableWidgetItem(QString::number(g.score, 'f', 0)),
                                                new QTableWidgetItem(signalLines.join(QLatin1Char('\n')))};
        if (g.students.size() > kListed) items[0]->setToolTip(g.students.join(QLatin1Char('\n')));
        items[1]->setToolTip(names.join(QLatin1Char('\n')));
        for (int c = 0; c < items.size(); ++c) {
            items[c]->setTextAlignment(Qt::AlignLeft | Qt::AlignTop);
            m_table->setItem(row, c, items[c]);
        }
        tintRow(m_table, row, levelColor(g.level));
    }
    m_table->resizeColumnsToContents();
    m_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    m_table->resizeRowsToContents();
}

void DuplicatesTab::openDetails(int row) {
    if (!m_batch || row < 0 || row >= static_cast<int>(m_groups.size())) return;
    QPointer<QDialog> dlg = m_open.value(row);
    if (!dlg) {
        const EvidenceGroup& g = m_groups[row];
        dlg = new QDialog(this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->setWindowTitle(tr("Possible cheating: %1").arg(g.students.join(QStringLiteral(", ")) +
                                                             (g.with.isEmpty() ? QString() : tr(" with %1").arg(g.with))));
        auto* lay = new QVBoxLayout(dlg);
        auto* text = new QTextBrowser(dlg);
        text->setHtml(detailsHtml(g));
        lay->addWidget(text);
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, dlg);
        connect(buttons, &QDialogButtonBox::rejected, dlg, &QDialog::close);
        lay->addWidget(buttons);
        dlg->resize(760, 620);
        m_open.insert(row, dlg);
        dlg->show();
    }
    dlg->raise();
    dlg->activateWindow();
}

QString DuplicatesTab::detailsHtml(const EvidenceGroup& g) const {
    const auto& E = m_batch->dupEntries;
    const QString who = g.students.join(QStringLiteral(", ")) + (g.with.isEmpty() ? QString() : tr(" with %1").arg(g.with));
    // One arg() call each: a later call would fill a "%1" inside a file or student name (D76).
    QString html = QStringLiteral("<h3>%1</h3><p>Level: <b>%2</b>, score %3</p>")
                       .arg(who.toHtmlEscaped(), strengthName(g.level), QString::number(g.score, 'f', 0));
    auto signalRows = [](const std::vector<Signal>& evidence) {
        QString rows;
        for (const Signal& s : evidence) {
            const QString label = s.shown();
            rows += QStringLiteral("<tr><td><b>%1</b></td><td>%2</td><td>weight %3</td></tr><tr><td colspan=3>%4</td></tr>")
                        .arg(label.toHtmlEscaped(), strengthName(s.strength), QString::number(s.weight, 'f', 0), s.detail.toHtmlEscaped());
        }
        return QStringLiteral("<table cellpadding=3>%1</table>").arg(rows);
    };
    // Each link under the short names, with the files' own names beneath.
    for (const int k : g.pairs) {
        const PairResult& p = m_batch->dups.pairs[k];
        html += QStringLiteral("<h4>%1 &harr; %2</h4><p style='color:#666'>%3<br>%4</p><p>%5, score %6</p>")
                    .arg(evidenceFileLabel(E[p.a]).toHtmlEscaped(), evidenceFileLabel(E[p.b]).toHtmlEscaped(),
                         evidenceFileName(E[p.a]).toHtmlEscaped(), evidenceFileName(E[p.b]).toHtmlEscaped(), strengthName(p.level),
                         QString::number(p.score, 'f', 0));
        html += signalRows(p.evidence);
    }
    if (!g.ownSignals.empty()) {
        html += QStringLiteral("<h4>%1</h4>").arg(tr("In this student's own files").toHtmlEscaped());
        html += signalRows(g.ownSignals);
    }
    QStringList paths;
    for (const int i : g.files)
        if (E[i].drawing)
            paths << QStringLiteral("%1: %2").arg(evidenceFileLabel(E[i]).toHtmlEscaped(), E[i].drawing->meta.path.toHtmlEscaped());
    html += QStringLiteral("<h4>%1</h4><p style='color:#666'>%2</p>").arg(tr("Files").toHtmlEscaped(), paths.join(QStringLiteral("<br>")));
    return html;
}

}  // namespace dcgui
