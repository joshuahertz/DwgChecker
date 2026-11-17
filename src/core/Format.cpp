#include "Format.h"

#include "DimText.h"

#include <cmath>

namespace dc {

static QString fixed(double v, int decimals) {
    QString s = QString::number(v, 'f', decimals);
    // Avoid "-0.000".
    bool allZero = true;
    for (QChar c : s)
        if (c.isDigit() && c != '0') { allZero = false; break; }
    if (allZero && s.startsWith('-')) s.remove(0, 1);
    return s;
}

QString fmtLen(double v, double other) {
    int dec = 3;
    if (std::isfinite(other))
        while (dec < 8 && fixed(v, dec) == fixed(other, dec)) ++dec;
    return fixed(v, dec);
}

QString fmtCoord(double v, double other) {
    int dec = 3;
    if (std::isfinite(other))
        while (dec < 8 && fixed(v, dec) == fixed(other, dec)) ++dec;
    QString s = fixed(v, dec);
    if (s.contains('.')) {
        while (s.endsWith('0') && !s.endsWith(".0")) s.chop(1);
    }
    return s;
}

QString fmtPt(Vec2 p) { return QStringLiteral("(%1, %2)").arg(fmtCoord(p.x), fmtCoord(p.y)); }

QString fmtOffset(Vec2 d) { return QStringLiteral("(%1, %2)").arg(fmtCoord(d.x, 0.0), fmtCoord(d.y, 0.0)); }

QString fmtDeg(double radians, double other) {
    const double deg = radians * 180.0 / kPi;
    int dec = 2;
    if (std::isfinite(other)) {
        const double o = other * 180.0 / kPi;
        while (dec < 6 && fixed(deg, dec) == fixed(o, dec)) ++dec;
    }
    return fixed(deg, dec) + QChar(0x00B0);
}

QString fmtAxes(const Primitive& p, const Primitive* o) {
    // More digits only for an axis that differs: fmtLen would print the same two to 8 places.
    auto axis = [&](double v, double other) { return o && v != other ? fmtLen(2 * v, 2 * other) : fmtLen(2 * v); };
    return QStringLiteral("%1 %2 %3")
        .arg(axis(p.radius, o ? o->radius : 0), QChar(0x00D7), axis(p.minorRadius, o ? o->minorRadius : 0));
}

QString lineDescriptor(const Primitive& p, bool forLinetypeError) {
    const QString kind = kindName(p.kind);
    switch (p.ltClass) {
    case LtClass::Visible:
        if (p.kind == PrimKind::Line) return forLinetypeError ? QStringLiteral("visible line") : QStringLiteral("line");
        return forLinetypeError ? QStringLiteral("visible ") + kind : kind;
    case LtClass::Hidden:
        if (p.kind == PrimKind::Line) return forLinetypeError ? QStringLiteral("hidden edge") : QStringLiteral("hidden line");
        return QStringLiteral("hidden ") + kind;
    case LtClass::Center: return p.kind == PrimKind::Line ? QStringLiteral("centerline") : QStringLiteral("center-linetype ") + kind;
    case LtClass::Phantom: return QStringLiteral("phantom ") + kind;
    case LtClass::Other:
        return QStringLiteral("%1 (linetype %2)").arg(kind, p.ltypeName.isEmpty() ? QStringLiteral("?") : p.ltypeName);
    }
    return kind;
}

QString describe(const Primitive& p) {
    switch (p.kind) {
    case PrimKind::Line: return QStringLiteral("%1 %2 long").arg(lineDescriptor(p), fmtLen(p.length()));
    case PrimKind::Circle: return QStringLiteral("%1 %2%3").arg(lineDescriptor(p), QChar(0x00D8), fmtLen(2 * p.radius));
    case PrimKind::Arc: return QStringLiteral("%1 R%2").arg(lineDescriptor(p), fmtLen(p.radius));
    case PrimKind::Ellipse:
    case PrimKind::EllipseArc: return QStringLiteral("%1 %2").arg(lineDescriptor(p), fmtAxes(p));
    case PrimKind::Spline:
        if (p.spline && p.spline->from == SplineFrom::FitPoints)
            return QStringLiteral("%1 through %2 points").arg(lineDescriptor(p)).arg(p.spline->fitCount());
        return QStringLiteral("%1 %2 long").arg(lineDescriptor(p), fmtLen(p.size()));
    case PrimKind::Dimension: return QStringLiteral("dimension %1").arg(fmtLen(p.measurement));
    case PrimKind::Text: return QStringLiteral("text \"%1\"").arg(p.text.left(30));
    case PrimKind::Hatch: return QStringLiteral("hatch %1").arg(p.text);
    case PrimKind::Unsupported: return p.sourceType;
    }
    return {};
}

QString describeGeometry(const Primitive& p) {
    switch (p.kind) {
    case PrimKind::Line: return QStringLiteral("%1 -> %2, length %3").arg(fmtPt(p.p0), fmtPt(p.p1), fmtLen(p.length()));
    case PrimKind::Circle: return QStringLiteral("center %1, radius %2").arg(fmtPt(p.center), fmtLen(p.radius));
    case PrimKind::Arc:
        return QStringLiteral("center %1, radius %2, %3 -> %4").arg(fmtPt(p.center), fmtLen(p.radius), fmtDeg(p.a0), fmtDeg(p.a1));
    case PrimKind::Ellipse:
    case PrimKind::EllipseArc: {
        QString t = QStringLiteral("center %1, axes %2, major axis at %3").arg(fmtPt(p.center), fmtAxes(p), fmtDeg(p.axisAngle));
        if (p.kind == PrimKind::EllipseArc) {
            // From the major axis, as AutoCAD's ELLIPSE asks for them: the angle to the point at
            // each end, not its parameter.
            auto from = [&](double t) { return normalizeAngle(std::atan2(p.minorRadius * std::sin(t), p.radius * std::cos(t))); };
            t += QStringLiteral(", %1 -> %2 from it").arg(fmtDeg(from(p.a0)), fmtDeg(from(p.a1)));
        }
        return t;
    }
    case PrimKind::Spline: {
        if (!p.spline) return {};
        const SplineData& s = *p.spline;
        auto list = [](const std::vector<Vec2>& v) {  // the first four and the last when there are many
            QStringList l;
            for (size_t i = 0; i < v.size(); ++i) {
                if (v.size() > 6 && i == 4) {
                    l << QStringLiteral("\u2026");
                    i = v.size() - 2;
                    continue;
                }
                l << fmtPt(v[i]);
            }
            return l.join(QStringLiteral(", "));
        };
        QString t;
        switch (s.from) {
        case SplineFrom::FitPoints: {
            const std::vector<Vec2> own(s.fit.begin(), s.fit.begin() + static_cast<std::ptrdiff_t>(s.fitCount()));
            t = QStringLiteral("through %1 (%2 points)").arg(list(own)).arg(own.size());
            break;
        }
        case SplineFrom::Polyline:
            t = QStringLiteral("spline-fit polyline of degree %1, vertices %2").arg(s.degree).arg(list(s.fit));
            break;
        case SplineFrom::ControlPoints:
            t = QStringLiteral("degree %1, %2 control points%3, from %4 to %5")
                    .arg(s.degree)
                    .arg(s.ctrl.size())
                    .arg(s.weights.empty() ? QString() : QStringLiteral(" (rational)"), fmtPt(p.p0), fmtPt(p.p1));
            break;
        }
        t += QStringLiteral(", %1 long").arg(fmtLen(s.length));
        if (s.closed(1e-9 * std::max(1.0, s.length))) t += QStringLiteral(", closed");
        return t;
    }
    case PrimKind::Text: return QStringLiteral("\"%1\" at %2").arg(p.text.left(40), fmtPt(p.center));
    case PrimKind::Dimension: {
        const QString shown = dimShownText(p);
        QString t = QStringLiteral("%1 = %2").arg(p.dimType, isAngularDimType(p.dimType) ? fmtDeg(p.measurement) : fmtLen(p.measurement));
        if (!shown.isEmpty()) t += QStringLiteral(", reads \"%1\"%2").arg(shown, dimTextIsTyped(p) ? QStringLiteral(" (typed in)") : QString());
        return t + QStringLiteral(" at %1").arg(fmtPt(p.center));
    }
    case PrimKind::Hatch: {
        const BBox b = p.bbox();
        return QStringLiteral("%1, %2 loop(s), %3 fill, box %4 - %5")
            .arg(p.text)
            .arg(p.loops ? p.loops->size() : 0)
            .arg(p.evenOdd ? QStringLiteral("even-odd") : QStringLiteral("outer-only"), fmtPt({b.x0, b.y0}), fmtPt({b.x1, b.y1}));
    }
    case PrimKind::Unsupported: return QStringLiteral("at %1").arg(fmtPt(p.center));
    }
    return {};
}

Vec2 messageLocation(const Primitive& p) {
    switch (p.kind) {
    case PrimKind::Line: return p.midpoint();
    default: return p.center;
    }
}

}  // namespace dc
