#pragma once
// Results tab: a tree with one row per student (Poss. dup., Student, Correctness,
// Errors, AutoCAD). A student with a single file is one row. A student with several
// files, or an assignment with several solution drawings, gets subrows: one per
// solution drawing (the file graded against it, or "no file"), then extra files.
// Clicking a file row opens the detail window; clicking the AutoCAD cell opens the
// file in AutoCAD; clicking a student row with subrows folds it (MainWindow).

#include "Batch.h"
#include "Csv.h"

#include <QAbstractItemModel>
#include <QColor>
#include <QStyledItemDelegate>

#include <memory>

namespace dcgui {

// a moved the share t (0-1) of the way to b, per channel: the table's subtle shades.
QColor blendColors(const QColor& a, const QColor& b, double t);

enum ResultsColumn { ColFlag = 0, ColName, ColCorrectness, ColErrors, ColAcad, ColCount };

enum ResultsRole {
    SortRole = Qt::UserRole + 1,
    StudentIndexRole,  // int: the file (index into BatchOutput::students), -1 = none
    FlagLevelRole,     // int (dc::Strength)
    LoadedRole,        // bool
    FileNameRole,      // QString: the gray second line of the Student column ("2 / 3 drawings"), often empty
    HasChildrenRole    // bool: a student row with subrows
};

class ResultsModel : public QAbstractItemModel {
    Q_OBJECT
public:
    using QAbstractItemModel::QAbstractItemModel;
    void setBatch(std::shared_ptr<const dc::BatchOutput> b);
    std::shared_ptr<const dc::BatchOutput> batch() const { return m_batch; }

    QModelIndex index(int row, int column, const QModelIndex& parent = {}) const override;
    QModelIndex parent(const QModelIndex& child) const override;
    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& = {}) const override { return ColCount; }
    QVariant data(const QModelIndex& idx, int role) const override;
    QVariant headerData(int section, Qt::Orientation o, int role) const override;

private:
    struct Sub {
        int file = -1;  // a student file, or -1 for a solution drawing without a file
        int ref = -1;   // the solution drawing (NoFile rows)
    };
    QVariant groupData(int g, int column, int role) const;
    QVariant fileData(int file, int column, int role, bool subrow) const;
    QVariant noFileData(int g, int ref, int column, int role) const;
    // "Drawing 2": a file row's name with several solution drawings (in the card's order).
    QString drawingLabel(int ref) const;

    std::shared_ptr<const dc::BatchOutput> m_batch;
    std::vector<dc::CopyFlag> m_flags;       // per file
    std::vector<dc::CopyFlag> m_groupFlags;  // per student: the strongest of their files
    std::vector<std::vector<Sub>> m_subs;    // per student: subrows (empty = one-row student)
};

// Paints the possible-duplicate flag as a warning icon, the student name with a gray
// second line, the correctness on a red-yellow-green scale, the error summary and the
// AutoCAD link.
class ResultsDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& idx) const override;
    QSize sizeHint(const QStyleOptionViewItem& opt, const QModelIndex& idx) const override;
};

}  // namespace dcgui
