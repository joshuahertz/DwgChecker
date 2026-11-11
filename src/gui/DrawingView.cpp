#include "DrawingView.h"

#include "DimText.h"
#include "Format.h"

#include <QGraphicsEllipseItem>
#include <QGraphicsLineItem>
#include <QGraphicsPathItem>
#include <QGraphicsRectItem>
#include <QGraphicsSimpleTextItem>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QScrollBar>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

using namespace dc;

namespace dcgui {

Qt::PenStyle penStyleFor(LtClass c) {
    switch (c) {
    case LtClass::Hidden: return Qt::DashLine;
    case LtClass::Center: return Qt::DashDotLine;
    case LtClass::Phantom: return Qt::DashDotDotLine;
    case LtClass::Other: return Qt::DotLine;
    case LtClass::Visible: return Qt::SolidLine;
    }
    return Qt::SolidLine;
}

QColor drawingColor(const Primitive& p, bool darkBackground) {
    if (p.aci == 7) return darkBackground ? QColor(Qt::white) : QColor(Qt::black);
    return QColor(static_cast<QRgb>(p.rgb));  // 0xRRGGBB, opaque
}

double asDrawnZ(const Primitive& p) {
    const bool label = p.kind == PrimKind::Text || p.kind == PrimKind::Dimension;
    return (label ? 1e9 : 0.0) + p.drawOrder;
}

DrawingView::DrawingView(QWidget* parent) : QGraphicsView(parent) {
    setScene(new QGraphicsScene(this));
    setRenderHint(QPainter::Antialiasing);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    setDragMode(QGraphicsView::NoDrag);
    setBackgroundBrush(QColor(250, 250, 250));
    setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
}

namespace {
// Zoom to all: 5% of the larger side around the drawing, room for the view labels that sit
// above the top views' boxes (about 18 px).
constexpr double kAllMargin = 0.05;
}

QGraphicsItem* DrawingView::addPrimitive(const Primitive& src, const QColor& color, double width, const Transform2D& t) {
    const Primitive p = transformed(src, t);
    m_extent.add(p.bbox());
    QPen pen(color, width, penStyleFor(p.ltClass), Qt::RoundCap);
    pen.setCosmetic(true);
    QGraphicsItem* item = nullptr;
    switch (p.kind) {
    case PrimKind::Line:
        item = scene()->addLine(QLineF(toScene(p.p0), toScene(p.p1)), pen);
        break;
    case PrimKind::Circle:
        item = scene()->addEllipse(QRectF(toScene({p.center.x - p.radius, p.center.y + p.radius}),
                                          QSizeF(2 * p.radius, 2 * p.radius)), pen);
        break;
    case PrimKind::Arc: {
        // With y negated, CAD angles map onto QPainterPath's visual (counter-clockwise) angles.
        const QRectF r(toScene({p.center.x - p.radius, p.center.y + p.radius}), QSizeF(2 * p.radius, 2 * p.radius));
        QPainterPath path;
        path.arcMoveTo(r, p.a0 * 180.0 / kPi);
        path.arcTo(r, p.a0 * 180.0 / kPi, p.sweep() * 180.0 / kPi);
        item = scene()->addPath(path, pen);
        break;
    }
    case PrimKind::Hatch: {
        QPainterPath path;
        path.setFillRule(p.evenOdd ? Qt::OddEvenFill : Qt::WindingFill);
        if (p.loops)
            for (const auto& l : *p.loops) {
                QPolygonF poly;
                for (Vec2 q : l) poly << toScene(q);
                path.addPolygon(poly);
                path.closeSubpath();
            }
        QColor fill = color;
        fill.setAlpha(60);
        QPen outline(color, 0.8, Qt::DotLine);
        outline.setCosmetic(true);
        auto* hi = scene()->addPath(path, outline, QBrush(fill));
        item = hi;
        break;
    }
    case PrimKind::Text:
    case PrimKind::Dimension: {
        // A dimension reads as its own drawing shows it: its style's precision and the
        // Ø / R prefix ("Ø0.750", "2.50", "90°"); the plain value when the style is unknown.
        QString label = p.text.left(24);
        if (p.kind == PrimKind::Dimension) {
            label = dimShownText(p);
            if (label.isEmpty()) label = isAngularDimType(p.dimType) ? fmtDeg(p.measurement) : fmtLen(p.measurement);
        }
        auto* ti = scene()->addSimpleText(label);
        ti->setBrush(color);
        ti->setPos(toScene(p.center));
        ti->setFlag(QGraphicsItem::ItemIgnoresTransformations);
        item = ti;
        break;
    }
    case PrimKind::Unsupported: {
        QPainterPath x;
        const QPointF c = toScene(p.center);
        const double s = std::max(0.05, 0.01 * std::max(m_extent.width(), m_extent.height()));
        x.moveTo(c + QPointF(-s, -s)); x.lineTo(c + QPointF(s, s));
        x.moveTo(c + QPointF(-s, s)); x.lineTo(c + QPointF(s, -s));
        pen.setStyle(Qt::SolidLine);
        item = scene()->addPath(x, pen);
        break;
    }
    }
    if (item) item->setToolTip(QStringLiteral("%1 on %2 (%3, %4)").arg(kindName(p.kind), p.layer, p.ltypeName, p.sourceType));
    return item;
}

void DrawingView::addBox(const BBox& b, const QColor& color, const QString& label) {
    if (!b.valid()) return;
    // A visible gap between the box and the items in it: 1% of what is drawn, under half of the
    // 2% gap that separates views when they are found, so that neighbouring boxes stay apart.
    const double pad = 0.01 * std::max(m_extent.diagonal(), b.diagonal());
    BBox box = b;
    box.x0 -= pad;
    box.y0 -= pad;
    box.x1 += pad;
    box.y1 += pad;
    m_extent.add(box);
    QPen pen(color, 1, Qt::DashLine);
    pen.setCosmetic(true);
    const QRectF r(toScene({box.x0, box.y1}), toScene({box.x1, box.y0}));
    auto* rect = scene()->addRect(r.normalized(), pen);
    rect->setZValue(-1);
    if (!label.isEmpty()) {
        // Outside the box, above its top-left corner. The label keeps its size at any zoom, so
        // it is lifted by its own height in pixels.
        auto* t = scene()->addSimpleText(label);
        t->setBrush(color);
        t->setPos(toScene({box.x0, box.y1}));
        t->setFlag(QGraphicsItem::ItemIgnoresTransformations);
        t->setTransform(QTransform::fromTranslate(0, -t->boundingRect().height() - 2));
    }
}

void DrawingView::fit(const BBox& b, double margin) {
    if (!b.valid()) return;
    const double w = std::max(b.width(), 1e-6), h = std::max(b.height(), 1e-6);
    const double m = std::max(w, h) * margin + 1e-9;
    const QRectF r = QRectF(toScene({b.x0 - m, b.y1 + m}), toScene({b.x1 + m, b.y0 - m})).normalized();
    // Room around the drawing, so that centering and panning never stop at the scene's edge.
    if (m_extent.valid()) {
        const QRectF all = QRectF(toScene({m_extent.x0, m_extent.y1}), toScene({m_extent.x1, m_extent.y0})).normalized();
        const double pad = 2 * std::max({all.width(), all.height(), 1e-6});
        scene()->setSceneRect(all.adjusted(-pad, -pad, pad, pad).united(r));
    }
    fitInView(r, Qt::KeepAspectRatio);
}

void DrawingView::zoomTo(const BBox& b, double margin) {
    m_target = {true, false, b, margin};
    fit(b, margin);
}

void DrawingView::zoomToAll() {
    m_target = {true, true, BBox(), kAllMargin};
    fit(m_extent, kAllMargin);
}

void DrawingView::clearDrawing() {
    scene()->clear();
    m_lassoItem = nullptr;
    m_extent = BBox();
}

void DrawingView::resizeEvent(QResizeEvent* e) {
    QGraphicsView::resizeEvent(e);
    if (m_target.active) fit(m_target.all ? m_extent : m_target.box, m_target.margin);
}

void DrawingView::wheelEvent(QWheelEvent* e) {
    m_target.active = false;
    const double f = std::pow(1.0015, e->angleDelta().y());
    scale(f, f);
}

void DrawingView::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::MiddleButton || e->button() == Qt::RightButton ||
        (e->button() == Qt::LeftButton && !m_lassoEnabled)) {
        m_panning = true;
        m_lastPan = e->pos();
        setCursor(Qt::ClosedHandCursor);
        e->accept();
        return;
    }
    if (e->button() == Qt::LeftButton && m_lassoEnabled) {
        m_lassoing = true;
        m_lasso.clear();
        m_lasso << mapToScene(e->pos());
        QPen pen(QColor(30, 90, 200), 1, Qt::DashLine);
        pen.setCosmetic(true);
        m_lassoItem = scene()->addPath(QPainterPath(), pen);
        m_lassoItem->setZValue(10);
        e->accept();
        return;
    }
    QGraphicsView::mousePressEvent(e);
}

void DrawingView::mouseMoveEvent(QMouseEvent* e) {
    if (m_panning) {
        const QPoint d = e->pos() - m_lastPan;
        m_lastPan = e->pos();
        if (!d.isNull()) m_target.active = false;
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - d.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - d.y());
        e->accept();
        return;
    }
    if (m_lassoing && m_lassoItem) {
        m_lasso << mapToScene(e->pos());
        QPainterPath path;
        path.addPolygon(m_lasso);
        path.closeSubpath();
        m_lassoItem->setPath(path);
        e->accept();
        return;
    }
    QGraphicsView::mouseMoveEvent(e);
}

void DrawingView::mouseReleaseEvent(QMouseEvent* e) {
    if (m_panning) {
        m_panning = false;
        unsetCursor();
        e->accept();
        return;
    }
    if (m_lassoing) {
        m_lassoing = false;
        if (m_lassoItem) {
            scene()->removeItem(m_lassoItem);
            delete m_lassoItem;
            m_lassoItem = nullptr;
        }
        QPolygonF cad;
        for (const QPointF& p : m_lasso) {
            const Vec2 v = fromScene(p);
            cad << QPointF(v.x, v.y);
        }
        if (cad.size() >= 3) emit lassoFinished(cad, e->modifiers() & Qt::ShiftModifier);
        e->accept();
        return;
    }
    QGraphicsView::mouseReleaseEvent(e);
}

}  // namespace dcgui
