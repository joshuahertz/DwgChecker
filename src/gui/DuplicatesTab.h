#pragma once
// Duplicates tab: flagged pairs with the per-pair signal breakdown, and clusters.
// Evidence only; nothing is labelled as cheating.

#include "Batch.h"

#include <QWidget>

#include <memory>

class QCheckBox;
class QLabel;
class QListWidget;
class QTableWidget;
class QTextBrowser;

namespace dcgui {

class DuplicatesTab : public QWidget {
    Q_OBJECT
public:
    explicit DuplicatesTab(QWidget* parent = nullptr);
    void setBatch(std::shared_ptr<const dc::BatchOutput> b);
    void showPair(int pairIndex);

private:
    void rebuild();
    void showDetails(int row);

    std::shared_ptr<const dc::BatchOutput> m_batch;
    QTableWidget* m_pairs;
    QTextBrowser* m_details;
    QListWidget* m_clusters;
    QLabel* m_notes;
    QCheckBox* m_showInfo;
    std::vector<int> m_rowToPair;
};

}  // namespace dcgui
