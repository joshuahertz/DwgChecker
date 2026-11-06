#pragma once
// Results tab: one row per student DWG (Student, AutoCAD, Copy flag, Correctness,
// Errors). Clicking a row opens the detail window; clicking the AutoCAD cell opens
// the file in AutoCAD (MainWindow handles both).

#include "Batch.h"
#include "Csv.h"

#include <QAbstractTableModel>
#include <QStyledItemDelegate>

#include <memory>

namespace dcgui {

enum ResultsColumn { ColName = 0, ColAcad, ColFlag, ColCorrectness, ColErrors, ColCount };

enum ResultsRole {
    SortRole = Qt::UserRole + 1,
    StudentIndexRole,
    FlagLevelRole,  // int (dc::Strength)
    LoadedRole,     // bool
    FileNameRole    // QString
};

class ResultsModel : public QAbstractTableModel {
    Q_OBJECT
public:
    using QAbstractTableModel::QAbstractTableModel;
    void setBatch(std::shared_ptr<const dc::BatchOutput> b);
    std::shared_ptr<const dc::BatchOutput> batch() const { return m_batch; }

    int rowCount(const QModelIndex& = {}) const override;
    int columnCount(const QModelIndex& = {}) const override { return ColCount; }
    QVariant data(const QModelIndex& idx, int role) const override;
    QVariant headerData(int section, Qt::Orientation o, int role) const override;

private:
    std::shared_ptr<const dc::BatchOutput> m_batch;
    std::vector<dc::CopyFlag> m_flags;
};

// Paints the student name with the file name under it, the AutoCAD link, the flag in
// color, the correctness on a red-yellow-green scale and the error summary.
class ResultsDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& idx) const override;
    QSize sizeHint(const QStyleOptionViewItem& opt, const QModelIndex& idx) const override;
};

}  // namespace dcgui
