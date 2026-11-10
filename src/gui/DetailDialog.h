#pragma once
// Detail view for one student: overlay, discrepancy list, metadata side by side
// and (Orthographic) per-view results and alignment checks.

#include "Batch.h"

#include <QColor>
#include <QDialog>

#include <memory>

class QComboBox;
class QListWidget;
class QTableWidget;
class QLabel;

namespace dcgui {

class DrawingView;

class DetailDialog : public QDialog {
    Q_OBJECT
public:
    DetailDialog(std::shared_ptr<const dc::BatchOutput> batch, int student, QWidget* parent = nullptr);
    void zoomToError(int errorIndex);
    // Selects the error in the list without zooming (a newly opened window shows everything).
    void selectError(int errorIndex);

private:
    void buildOverlay();
    QColor viewColor(int view) const;  // the view's status color (Orthographic)
    void buildMetadata(QTableWidget* t);
    void buildViews(QTableWidget* t);
    QWidget* buildPrimitivesPage(QWidget* parent);
    void fillPrimitives();
    void zoomToPrimitive(int row);
    dc::BBox errorBox(int errorIndex) const;

    std::shared_ptr<const dc::BatchOutput> m_batch;
    int m_student;
    DrawingView* m_view = nullptr;
    QListWidget* m_errors = nullptr;
    QComboBox* m_primSource = nullptr;  // student / reference
    QTableWidget* m_prims = nullptr;
    QLabel* m_primWarnings = nullptr;
};

}  // namespace dcgui
