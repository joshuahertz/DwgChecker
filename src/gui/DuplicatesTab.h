#pragma once
// Possible cheating tab: a row per group of students linked by evidence (D82), across the whole
// tab; a click on a row opens a window with every link between them and its signals (D84).
// Evidence only; nothing is labelled as cheating.

#include "Batch.h"
#include "Duplicates.h"

#include <QHash>
#include <QPointer>
#include <QWidget>

#include <memory>
#include <vector>

class QCheckBox;
class QDialog;
class QLabel;
class QTableWidget;

namespace dcgui {

class DuplicatesTab : public QWidget {
    Q_OBJECT
public:
    explicit DuplicatesTab(QWidget* parent = nullptr);
    void setBatch(std::shared_ptr<const dc::BatchOutput> b);

private:
    void rebuild();
    void openDetails(int row);  // one window per row; a second click brings it to the front
    QString detailsHtml(const dc::EvidenceGroup& g) const;
    void closeDetails();

    std::shared_ptr<const dc::BatchOutput> m_batch;
    std::vector<dc::EvidenceGroup> m_groups;  // one per row
    QHash<int, QPointer<QDialog>> m_open;     // by row
    QTableWidget* m_table;
    QLabel* m_notes;
    QCheckBox* m_showInfo;
};

}  // namespace dcgui
