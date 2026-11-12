#include "Model.h"

#include <algorithm>

#include <QDate>

#include <QTime>
#include <QTimeZone>

namespace dc {

QDateTime DwgTime::toDateTime() const {
    if (!valid) return {};
    const QDate date = QDate::fromJulianDay(day);
    return QDateTime(date, QTime(0, 0), QTimeZone::UTC).addMSecs(ms);
}

QString DwgTime::toString() const {
    if (!valid) return QStringLiteral("-");
    return toDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
}

QString insUnitsName(int u) {
    switch (u) {
    case 0: return QStringLiteral("unitless");
    case 1: return QStringLiteral("inches");
    case 2: return QStringLiteral("feet");
    case 3: return QStringLiteral("miles");
    case 4: return QStringLiteral("millimeters");
    case 5: return QStringLiteral("centimeters");
    case 6: return QStringLiteral("meters");
    case 7: return QStringLiteral("kilometers");
    case 8: return QStringLiteral("microinches");
    case 9: return QStringLiteral("mils");
    case 10: return QStringLiteral("yards");
    case 13: return QStringLiteral("microns");
    case 14: return QStringLiteral("decimeters");
    default: return u < 0 ? QStringLiteral("unknown") : QStringLiteral("units code %1").arg(u);
    }
}

QString dwgVersionName(const QString& v) {
    static const QList<QPair<QString, QString>> map = {
        {"AC1009", "R12"}, {"AC1012", "R13"}, {"AC1014", "R14"}, {"AC1015", "R2000"},
        {"AC1018", "R2004"}, {"AC1021", "R2007"}, {"AC1024", "R2010"}, {"AC1027", "R2013"},
        {"AC1032", "R2018"}};
    for (const auto& [k, n] : map)
        if (k == v) return n;
    return v;
}

QString kindName(PrimKind k) {
    switch (k) {
    case PrimKind::Line: return QStringLiteral("line");
    case PrimKind::Arc: return QStringLiteral("arc");
    case PrimKind::Circle: return QStringLiteral("circle");
    case PrimKind::Text: return QStringLiteral("text");
    case PrimKind::Dimension: return QStringLiteral("dimension");
    case PrimKind::Hatch: return QStringLiteral("hatch");
    case PrimKind::Unsupported: return QStringLiteral("unsupported");
    }
    return {};
}

QString ltClassName(LtClass c) {
    switch (c) {
    case LtClass::Visible: return QStringLiteral("visible");
    case LtClass::Hidden: return QStringLiteral("hidden");
    case LtClass::Center: return QStringLiteral("center");
    case LtClass::Phantom: return QStringLiteral("phantom");
    case LtClass::Other: return QStringLiteral("other");
    }
    return {};
}

bool isGeometric(PrimKind k) { return k == PrimKind::Line || k == PrimKind::Arc || k == PrimKind::Circle; }
bool isAngularDimType(const QString& t) { return t.startsWith(QLatin1String("ANG")); }

bool isLinearDimType(const QString& t) { return t == QLatin1String("LINEAR") || t == QLatin1String("ALIGNED"); }

bool sameDimFamily(const QString& a, const QString& b) { return a == b || (isLinearDimType(a) && isLinearDimType(b)); }

double Primitive::sweep() const {
    if (kind == PrimKind::Circle) return kTwoPi;
    if (kind != PrimKind::Arc) return 0;
    const double s = ccwSweep(a0, a1);
    return s == 0 ? kTwoPi : s;
}

Vec2 Primitive::midpoint() const {
    switch (kind) {
    case PrimKind::Line: return (p0 + p1) * 0.5;
    case PrimKind::Arc: return center + fromAngle(a0 + sweep() / 2) * radius;
    default: return center;
    }
}

BBox Primitive::bbox() const {
    BBox b;
    switch (kind) {
    case PrimKind::Line: b.add(p0); b.add(p1); break;
    case PrimKind::Circle:
        b.add(center - Vec2{radius, radius}); b.add(center + Vec2{radius, radius});
        break;
    case PrimKind::Arc: {
        b.add(arcStart()); b.add(arcEnd());
        const double sw = sweep();
        for (int q = 0; q < 4; ++q) {
            const double ang = q * kPi / 2;
            if (ccwSweep(a0, ang) <= sw) b.add(center + fromAngle(ang) * radius);
        }
        break;
    }
    case PrimKind::Dimension: b.add(center); b.add(p0); b.add(p1); break;
    case PrimKind::Hatch:
        if (loops)
            for (const auto& l : *loops)
                for (Vec2 q : l) b.add(q);
        if (!b.valid()) b.add(center);
        break;
    default: b.add(center); break;
    }
    return b;
}

double Primitive::size() const {
    switch (kind) {
    case PrimKind::Line: return length();
    case PrimKind::Arc: return radius * sweep();
    case PrimKind::Circle: return radius * kTwoPi;
    default: return 0;
    }
}

Primitive transformed(const Primitive& p, const Transform2D& t) {
    if (t.isIdentity()) return p;  // keep the exact bits
    Primitive r = p;
    r.p0 = t.apply(p.p0);
    r.p1 = t.apply(p.p1);
    r.center = t.apply(p.center);
    if (p.hasDimCenter) r.dimCenter = t.apply(p.dimCenter);
    if (p.dimPlace) {
        auto q = std::make_shared<DimPlace>(*p.dimPlace);
        q->dir = t.applyLinear(q->dir);
        q->side0 = t.applyLinear(q->side0);
        q->side1 = t.applyLinear(q->side1);
        for (Vec2* e : {&q->edge0a, &q->edge0b, &q->edge1a, &q->edge1b}) *e = t.apply(*e);
        r.dimPlace = std::move(q);
    }
    // How a dimension is drawn is shown only where a file is shown as drawn, never moved: a
    // moved copy goes without, rather than copying and moving it for nothing (D88).
    r.dimLook.reset();
    // Uniform scale of a similarity transform (exactly 1 for rigid ones).
    double sc = std::sqrt(std::fabs(t.det()));
    if (std::fabs(sc - 1.0) < 1e-12) sc = 1.0;
    if (sc != 1.0) {
        r.radius = p.radius * sc;
        if (p.kind == PrimKind::Dimension && !isAngularDimType(p.dimType)) r.measurement = p.measurement * sc;
    }
    if (p.kind == PrimKind::Hatch && p.loops) {
        auto out = std::make_shared<std::vector<std::vector<Vec2>>>(*p.loops);
        for (auto& l : *out)
            for (Vec2& q : l) q = t.apply(q);
        r.loops = std::move(out);
    }
    if (p.kind == PrimKind::Arc) {
        const double phi = t.rotationAngle();
        if (t.isMirrored()) {
            // T = R(phi) * diag(-1, 1): angle t maps to phi + pi - t and orientation reverses,
            // so CCW a0->a1 becomes CCW (phi+pi-a1)->(phi+pi-a0).
            r.a0 = normalizeAngle(phi + kPi - p.a1);
            r.a1 = normalizeAngle(phi + kPi - p.a0);
        } else {
            r.a0 = normalizeAngle(p.a0 + phi);
            r.a1 = normalizeAngle(p.a1 + phi);
        }
    }
    return r;
}

bool hatchContains(const Primitive& h, Vec2 q) {
    if (!h.loops) return false;
    int crossings = 0;
    bool inAny = false;
    for (const auto& l : *h.loops) {
        bool in = false;
        const size_t n = l.size();
        for (size_t i = 0, j = n - 1; i < n; j = i++) {
            const Vec2 a = l[i], b = l[j];
            if ((a.y > q.y) != (b.y > q.y) && q.x < (b.x - a.x) * (q.y - a.y) / (b.y - a.y) + a.x) in = !in;
        }
        crossings += in;
        inAny = inAny || in;
    }
    return h.evenOdd ? (crossings % 2) == 1 : inAny;
}

double hatchBoundaryDistance(const Primitive& h, Vec2 q) {
    double d = 1e300;
    if (!h.loops) return d;
    for (const auto& l : *h.loops) {
        const size_t n = l.size();
        for (size_t i = 0, j = n - 1; i < n; j = i++) d = std::min(d, pointSegmentDistance(q, l[j], l[i]));
    }
    return d;
}

BBox Drawing::bbox(bool geometricOnly) const {
    BBox b;
    for (const auto& p : prims)
        if (!geometricOnly || isGeometric(p.kind)) b.add(p.bbox());
    return b;
}

int Drawing::count(PrimKind k) const {
    int n = 0;
    for (const auto& p : prims) n += p.kind == k;
    return n;
}

void Drawing::addWarning(WarnKind k, const QString& text, std::vector<int> prims_) {
    warnings.push_back({k, text, std::move(prims_)});
}

}  // namespace dc
