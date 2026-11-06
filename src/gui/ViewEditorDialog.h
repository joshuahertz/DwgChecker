#pragma once
// Reference view-assignment editor (Orthographic mode): each view in its own
// color, gap slider with live re-segmentation, lasso to reassign primitives,
// view names, and saving the <ref>.views.json sidecar.

#include "Ortho.h"

#include <QDialog>

#include <set>

class QLabel;
class QComboBox;
class QSlider;
class QTableWidget;

namespace dcgui {

class DrawingView;

class ViewEditorDialog : public QDialog {
    Q_OBJECT
public:
    ViewEditorDialog(dc::DrawingPtr ref, const QString& refPath, const dc::ViewSet& initial, QWidget* parent = nullptr);
    const dc::ViewSet& viewSet() const { return m_vs; }
    double gapFraction() const { return m_vs.gapFraction; }

private:
    void redraw();
    void refreshTable();
    void onLasso(const QPolygonF& poly, bool add);
    void assignSelection();
    void addView();
    void save();
    static QColor colorFor(int view);

    dc::DrawingPtr m_ref;
    QString m_refPath;
    dc::ViewSet m_vs;
    std::set<int> m_selected;
    DrawingView* m_view;
    QTableWidget* m_table;
    QSlider* m_gap;
    QLabel* m_gapLabel;
    QComboBox* m_target;
    QLabel* m_status;
};

}  // namespace dcgui
