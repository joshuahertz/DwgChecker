#pragma once
// Detail view for one student: the drawing in three tabs (the overlay on the solution, the
// student's file as submitted, the reference solution as drawn), discrepancy list, metadata
// side by side and (Orthographic) per-view results and alignment checks. Clicking an error or
// a primitive zooms all three tabs.

#include "Batch.h"

#include <QColor>
#include <QDialog>

#include <memory>
#include <optional>

class QCheckBox;
class QComboBox;
class QListWidget;
class QTabWidget;
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
    // The two files as they are: the student's as submitted (its own coordinates, scale and
    // layout; nothing moved, rescaled or rearranged) and the reference solution as drawn.
    // Everything in them, dimensions included, each item in its own color.
    void buildRaw();
    void zoomRef(const dc::BBox& b);  // the solution tab (reference frame, as the overlay)
    void updateLegend();  // for the drawing tab shown
    QColor viewColor(int view) const;  // the view's status color (Orthographic)
    void buildMetadata(QTableWidget* t);
    void buildViews(QTableWidget* t);
    QWidget* buildPrimitivesPage(QWidget* parent);
    void fillPrimitives();
    void zoomToPrimitive(int row);
    dc::BBox errorBox(int errorIndex) const;     // reference frame (the overlay)
    dc::BBox rawErrorBox(int errorIndex) const;  // the student's file (the raw tab)
    // Where reference item i is, or should be, in the student's file: its partner, else the
    // item moved back by the transform its view was graded with.
    dc::BBox rawBoxOfRef(int refPrim) const;
    // Reference frame -> student's file for a view (-1 when not Orthographic), from the
    // transforms of the student items graded in it; none when the view wasn't found.
    std::optional<dc::Transform2D> refToStudent(int view) const;
    void zoomRaw(const dc::BBox& b);  // with some context around it; invalid: zoom to all

    std::shared_ptr<const dc::BatchOutput> m_batch;
    int m_student;
    QTabWidget* m_viewTabs = nullptr;
    DrawingView* m_view = nullptr;    // the overlay
    DrawingView* m_raw = nullptr;     // the student's file as submitted
    DrawingView* m_refView = nullptr; // the reference solution as drawn
    QCheckBox* m_rawDark = nullptr;
    QLabel* m_legend = nullptr;
    QListWidget* m_errors = nullptr;
    QComboBox* m_primSource = nullptr;  // student / reference
    QTableWidget* m_prims = nullptr;
    QLabel* m_primWarnings = nullptr;
};

}  // namespace dcgui
