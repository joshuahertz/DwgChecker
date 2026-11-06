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

    // Fits the given box (reference coordinates) with a margin.
    void zoomTo(const dc::BBox& b, double marginFraction = 0.25);
    // Fits everything drawn with a small margin. Until the user zooms or pans, the
    // view stays fitted when it is resized (so it opens fitted to its real size).
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
    bool m_autoFit = false;
    bool m_panning = false;
    QPoint m_lastPan;
    bool m_lassoEnabled = false;
    bool m_lassoing = false;
    QPolygonF m_lasso;
    QGraphicsPathItem* m_lassoItem = nullptr;
};

// Pen style for a linetype class (dash patterns are cosmetic, so they stay readable at any zoom).
Qt::PenStyle penStyleFor(dc::LtClass c);

}  // namespace dcgui
