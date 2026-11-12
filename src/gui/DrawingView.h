#pragma once
// QGraphicsView for CAD geometry: y axis up, wheel zoom around the cursor,
// middle/right-button pan, cosmetic pens. Optional lasso selection.

#include "Model.h"

#include <QGraphicsView>
#include <QPainterPath>

class QGraphicsItem;
class QGraphicsPathItem;

namespace dcgui {

class DrawingView : public QGraphicsView {
    Q_OBJECT
public:
    explicit DrawingView(QWidget* parent = nullptr);

    // Adds one primitive (transformed into the reference frame by `t`) to the scene.
    QGraphicsItem* addPrimitive(const dc::Primitive& p, const QColor& color, double width = 1.5,
                                const dc::Transform2D& t = dc::Transform2D::identity());
    void addBox(const dc::BBox& b, const QColor& color, const QString& label = QString());
    // Replaces what is drawn with the whole file as AutoCAD shows it: every item in its own
    // color and linetype, in its draw order, on model space's dark or light background. The
    // zoom and pan stay. Used by the preview and the detail view's file tabs.
    void showAsDrawn(const dc::Drawing* d, bool dark);

    // Fits the given box (drawing coordinates) with a margin.
    void zoomTo(const dc::BBox& b, double marginFraction = 0.25);
    // Fits everything drawn with a small margin.
    // Either fit is redone when the view is resized, until the user zooms or pans: the view
    // opens fitted to its real size, and one zoomed while hidden (another tab) shows that zoom.
    void zoomToAll();
    void clearDrawing();

    void setLassoEnabled(bool on) { m_lassoEnabled = on; }

    static QPointF toScene(dc::Vec2 p) { return {p.x, -p.y}; }
    static dc::Vec2 fromScene(QPointF p) { return {p.x(), -p.y()}; }

signals:
    // Lasso finished: polygon in reference (CAD) coordinates.
    void lassoFinished(const QPolygonF& cadPolygon, bool addToSelection);

protected:
    void resizeEvent(QResizeEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;

private:
    void fit(const dc::BBox& b, double marginFraction);

    // Extent of what was drawn, from the geometry itself: Qt's item bounds count
    // cosmetic pen widths and fixed-size labels as drawing units.
    dc::BBox m_extent;
    // The last fit asked for, redone on resize while active (until the user zooms or pans).
    struct FitTarget {
        bool active = false;
        bool all = true;  // everything drawn (m_extent as it is then), else `box`
        dc::BBox box;
        double margin = 0;
    } m_target;
    bool m_panning = false;
    QPoint m_lastPan;
    bool m_lassoEnabled = false;
    bool m_lassoing = false;
    QPolygonF m_lasso;
    QGraphicsPathItem* m_lassoItem = nullptr;
};

// Pen style for a linetype class (dash patterns are cosmetic, so they stay readable at any zoom).
Qt::PenStyle penStyleFor(dc::LtClass c);

// A primitive's own color, as AutoCAD shows it: its resolved ACI or true color, with ACI 7
// (black/white) in the color that contrasts with the background.
QColor drawingColor(const dc::Primitive& p, bool darkBackground);
// The stacking (z value) for a file shown as it is: AutoCAD's draw order (DRAWORDER), with
// text and dimension values, which are only labels here, on top. Pieces of one entity share
// it and stack in the order they are added.
double asDrawnZ(const dc::Primitive& p);
// AutoCAD's default model space background, and the light alternative.
inline const QColor kModelSpaceDark(33, 40, 48);
inline const QColor kModelSpaceLight(255, 255, 255);

}  // namespace dcgui
