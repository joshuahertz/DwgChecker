// Geometry, matching, alignment, identity, formatting, settings and CSV.

#include "Aligner.h"
#include "Archive.h"
#include "Batch.h"
#include "ClassSummary.h"
#include "Color.h"
#include "Comparator.h"
#include "Creative.h"
#include "Csv.h"
#include "Format.h"
#include "Matcher.h"
#include "Readability.h"
#include "Settings.h"
#include "StudentIdentity.h"
#include "TestUtil.h"
#include "ViewSync.h"

#include <QTemporaryDir>
#include <QtTest>

using namespace dc;
using namespace dct;

namespace {

Primitive line(Vec2 a, Vec2 b, LtClass c = LtClass::Visible) {
    Primitive p;
    p.kind = PrimKind::Line;
    p.p0 = a;
    p.p1 = b;
    p.ltClass = c;
    return p;
}
Primitive circle(Vec2 c, double r) {
    Primitive p;
    p.kind = PrimKind::Circle;
    p.center = c;
    p.radius = r;
    return p;
}
Primitive arc(Vec2 c, double r, double a0, double a1) {
    Primitive p;
    p.kind = PrimKind::Arc;
    p.center = c;
    p.radius = r;
    p.a0 = normalizeAngle(a0);
    p.a1 = normalizeAngle(a1);
    return p;
}
// The ellipse c + U cos(t) + V sin(t), whole, or from t0 to t1.
Primitive ellipse(Vec2 c, Vec2 U, Vec2 V, double t0 = 0, double t1 = 0, bool full = true) {
    Primitive p;
    setEllipse(p, c, U, V, t0, t1, full);
    return p;
}
// A fit-point spline through pts (closed: back to the first).
Primitive spline(std::vector<Vec2> pts, bool closed = false) {
    if (closed) pts.push_back(pts.front());
    Primitive p;
    p.kind = PrimKind::Spline;
    p.spline = splineThroughPoints(pts, 0, {}, {});
    p.p0 = p.spline->start();
    p.p1 = p.spline->end();
    p.center = p.spline->middle();
    return p;
}
std::vector<int> all(const std::vector<Primitive>& v) {
    std::vector<int> i(v.size());
    for (size_t k = 0; k < v.size(); ++k) i[k] = static_cast<int>(k);
    return i;
}

}  // namespace

class TestMisc : public QObject {
    Q_OBJECT
private slots:
    void transformBasics();
    void mirroredArcTransform();
    void ellipseFromItsAxes();
    void ellipseTransform();
    void ellipseMatching();
    void splineMatching();
    void readabilityEdgeCases();
    void viewSyncMapping();
    void creativeKinds();
    void creativeGrading();
    void creativeCenterlineRule();
    void creativeCornerRule();
    void creativeChoicesSaved();
    void lineMatchEitherOrder();
    void arcMatchAngles();
    void looseCenterlines();
    void greedyOneToOne();
    void nearMissPairing();
    void supersetLine();
    void alignRecoversRigidTransform();
    void alignCircleOnlyDrawing();
    void alignMirror();
    void alignEllipseOnlyDrawing();
    void alignSplineOnlyDrawing();
    void identityCanvas();
    void identityFallback();
    void identityAccentedNames();
    void zipSubmissions();
    void zipThatCannotBeRead();
    void classSummaryCounts();
    void classSummaryColorScale();
    void formatting();
    void settingsRoundTrip();
    void csvEscaping();
    void dimensionsChecked();
    void displacedCopiesArePaired();
    void displacedLineTurnedSlightly();
    void tinyShiftShowsItsSize();
    void errorSummaryCounts();
    void similarityTransforms();
    void scaleCandidateList();
    void unitlessFileHasNoUnitsWarning();
    void singularInverseIsIdentity();
};

void TestMisc::transformBasics() {
    const Transform2D t = Transform2D::rigid(deg(30), 5, -2);
    const Vec2 p = t.apply({1, 0});
    QVERIFY(dist(p, {5 + std::cos(deg(30)), -2 + std::sin(deg(30))}) < 1e-12);
    const Vec2 back = t.inverse().apply(p);
    QVERIFY(dist(back, {1, 0}) < 1e-12);
    QVERIFY(std::fabs(t.rotationAngle() - deg(30)) < 1e-12);
    const Transform2D m = Transform2D::rigid(deg(30), 0, 0, true);
    QVERIFY(m.isMirrored());
    QVERIFY(std::fabs(m.rotationAngle() - deg(30)) < 1e-12);
    QVERIFY(Transform2D::identity().isIdentity());
}

void TestMisc::mirroredArcTransform() {
    // Arc 0..90 mirrored in x becomes 90..180.
    const Primitive a = arc({0, 0}, 1, 0, kPi / 2);
    const Primitive m = transformed(a, Transform2D::mirrorX());
    QVERIFY(angleDiff(m.a0, kPi / 2) < 1e-12);
    QVERIFY(angleDiff(m.a1, kPi) < 1e-12);
    // Start/end points map onto the mirrored points.
    QVERIFY(dist(m.arcEnd(), {-1, 0}) < 1e-12);
    QVERIFY(dist(m.arcStart(), {0, 1}) < 1e-12);
}

void TestMisc::ellipseFromItsAxes() {
    // Two semi-diameters that are not its axes: the circle r1 of a block scaled (1, 0.5) and
    // turned 30 degrees has them at (cos 30, sin 30) and 0.5 (-sin 30, cos 30), so the axes
    // here, sheared by 0.2 more, are found again.
    const Vec2 u = fromAngle(deg(30)), v = u.perp() * 0.5;
    const Primitive e = ellipse({1, 2}, u + v * 0.2, v);
    QCOMPARE(e.kind, PrimKind::Ellipse);
    // Its points are those of c + U cos(t) + V sin(t), and it is no wider than they are.
    double far = 0, near = 1e9;
    for (int k = 0; k < 720; ++k) {
        const double t = kTwoPi * k / 720;
        const Vec2 q = Vec2{1, 2} + (u + v * 0.2) * std::cos(t) + v * std::sin(t);
        far = std::max(far, dist(q, {1, 2}));
        near = std::min(near, dist(q, {1, 2}));
        QVERIFY(e.bbox().contains(q, 1e-12));
    }
    QVERIFY(std::fabs(e.radius - far) < 1e-4 && std::fabs(e.minorRadius - near) < 1e-4);
    QVERIFY(e.axisAngle >= 0 && e.axisAngle < kPi);
    // The axes given: taken as they are, the major one turned into [0, pi).
    const Primitive f = ellipse({0, 0}, {-2, 0}, {0, -1});
    QCOMPARE(f.radius, 2.0);
    QCOMPARE(f.minorRadius, 1.0);
    QCOMPARE(f.axisAngle, 0.0);
    // Traced clockwise (V the other way): counter-clockwise from the other end.
    const Primitive g = ellipse({0, 0}, {2, 0}, {0, -1}, 0, kPi / 2, false);
    QCOMPARE(g.kind, PrimKind::EllipseArc);
    QVERIFY(dist(g.arcStart(), {0, -1}) < 1e-12);
    QVERIFY(dist(g.arcEnd(), {2, 0}) < 1e-12);
}

void TestMisc::ellipseTransform() {
    const Primitive e = ellipse({1, 2}, {2, 0}, {0, 1}, 0.25, 2.0, false);
    for (const Transform2D& t : {Transform2D::rigid(deg(37), 3, -1), Transform2D::rigid(deg(200), 0, 5, true),
                                 Transform2D::scaling(2.5), Transform2D::mirrorX()}) {
        const Primitive m = transformed(e, t);
        QCOMPARE(m.kind, PrimKind::EllipseArc);
        QVERIFY(m.axisAngle >= 0 && m.axisAngle < kPi);
        QVERIFY(std::fabs(m.radius - 2 * t.scaleFactor()) < 1e-12);
        QVERIFY(std::fabs(m.minorRadius - t.scaleFactor()) < 1e-12);
        // The same curve: the moved ends (swapped by a mirror) and the moved middle.
        const Vec2 s = t.apply(e.arcStart()), f = t.apply(e.arcEnd());
        QVERIFY(dist(m.arcStart(), t.isMirrored() ? f : s) < 1e-12);
        QVERIFY(dist(m.arcEnd(), t.isMirrored() ? s : f) < 1e-12);
        QVERIFY(dist(m.midpoint(), t.apply(e.midpoint())) < 1e-12);
    }
    const Primitive w = transformed(ellipse({0, 0}, {2, 0}, {0, 1}), Transform2D::rigid(deg(100), 1, 1));
    QCOMPARE(w.kind, PrimKind::Ellipse);
    QVERIFY(std::fabs(w.axisAngle - deg(100)) < 1e-12);
}

void TestMisc::ellipseMatching() {
    MatchCriteria c;
    c.linTol = 1e-6;
    const Primitive e = ellipse({1, 1}, {2, 0}, {0, 1});
    QVERIFY(geometryMatches(e, ellipse({1, 1}, {-2, 0}, {0, 1}), c));             // the axis from its other end
    QVERIFY(geometryMatches(e, transformed(e, Transform2D::rigid(deg(180), 2, 2)), c));  // turned half way round about its center
    QVERIFY(!geometryMatches(e, transformed(e, Transform2D::rigid(deg(1e-3), 0, 0)), c));  // turned about the origin
    QVERIFY(!geometryMatches(e, ellipse({1, 1}, {2, 0}, {0, 1.00001}), c));
    QVERIFY(!geometryMatches(e, circle({1, 1}, 1.5), c));
    // A turn that moves it less than the tolerance: nearly round.
    const Primitive round = ellipse({0, 0}, {1, 0}, {0, 1 - 1e-5});
    QVERIFY(geometryMatches(round, ellipse({0, 0}, fromAngle(deg(0.005)), fromAngle(deg(0.005)).perp() * (1 - 1e-5)), c));
    // Arcs: the ends must agree too.
    const Primitive a = ellipse({0, 0}, {2, 0}, {0, 1}, 0, kPi / 2, false);
    QVERIFY(geometryMatches(a, ellipse({0, 0}, {-2, 0}, {0, -1}, kPi, 1.5 * kPi, false), c));
    QVERIFY(!geometryMatches(a, ellipse({0, 0}, {2, 0}, {0, 1}, 0, kPi / 2 + 1e-4, false), c));
    // Near misses: a circle for an ellipse pairs with it (the wrong shape), the closest first.
    std::vector<Primitive> ref{e, a};
    std::vector<Primitive> stu{circle({1, 1}, 1.5), ellipse({0.1, 0}, {2, 0}, {0, 1}, 0, kPi / 2, false)};
    const auto pairs = pairNearMisses(ref, all(ref), stu, all(stu), 0.5, c);
    QCOMPARE(pairs.size(), size_t(2));
    for (const auto& p : pairs) QCOMPARE(p.ref, p.stu);
    // The same axes elsewhere: displaced.
    std::vector<Primitive> far{transformed(e, Transform2D::translation(20, 0))};
    const auto moved = pairNearMisses(ref, {0}, far, {0}, 0.5, c);
    QCOMPARE(moved.size(), size_t(1));
    QVERIFY(moved[0].displaced);
}

void TestMisc::splineMatching() {
    MatchCriteria c;
    c.linTol = 1e-6;
    const std::vector<Vec2> q{{0, 0}, {1, 1}, {2, 0.5}, {3, 1.5}, {4, 0}};
    const Primitive a = spline(q);
    QVERIFY(geometryMatches(a, spline({q.rbegin(), q.rend()}), c));  // drawn the other way
    Primitive cv = a;  // the same curve given by its control points
    cv.spline = splineFromControlPoints(3, a.spline->knots, a.spline->ctrl, {});
    QVERIFY(geometryMatches(a, cv, c));
    QVERIFY(geometryMatches(a, transformed(transformed(a, Transform2D::rigid(deg(37), 2, 1)), Transform2D::rigid(deg(37), 2, 1).inverse()), c));
    std::vector<Vec2> off = q;
    off[2].y += 1e-4;
    QVERIFY(!geometryMatches(a, spline(off), c));
    QVERIFY(!geometryMatches(a, transformed(a, Transform2D::translation(2e-6, 0)), c));
    QVERIFY(!geometryMatches(a, spline(q, true), c));  // closed
    // Moved and turned a little, far away: the same shape, displaced.
    std::vector<Primitive> ref{a};
    std::vector<Primitive> far{transformed(a, Transform2D::rigid(deg(2), 30, 0))};
    const auto moved = pairNearMisses(ref, {0}, far, {0}, 0.5, c);
    QCOMPARE(moved.size(), size_t(1));
    QVERIFY(moved[0].displaced);
    // Formatting (D99).
    QCOMPARE(describe(a), QStringLiteral("spline through 5 points"));
    QVERIFY2(describeGeometry(a).startsWith(QStringLiteral("through (0.0, 0.0), (1.0, 1.0), (2.0, 0.5), (3.0, 1.5), (4.0, 0.0) (5 points), ")),
             qPrintable(describeGeometry(a)));
    QVERIFY(describeGeometry(spline(q, true)).endsWith(QStringLiteral(", closed")));
    // Its outline is kept with it, and moves with it (D105).
    const Primitive flipped = transformed(a, Transform2D::rigid(deg(37), 2, 1, true));
    BBox box;
    for (Vec2 p : flipped.spline->sp) box.add(p);
    const BBox kept = flipped.bbox();
    QVERIFY(kept.x0 == box.x0 && kept.y0 == box.y0 && kept.x1 == box.x1 && kept.y1 == box.y1);
    QCOMPARE(describe(cv), QStringLiteral("spline %1 long").arg(fmtLen(a.size())));
}

void TestMisc::readabilityEdgeCases() {
    ReadOptions o;
    o.dimensions = true;
    Primitive dim;
    dim.kind = PrimKind::Dimension;
    dim.dimType = QStringLiteral("LINEAR");
    dim.p0 = {0, 0};
    dim.p1 = {4, 0};
    dim.center = {2, 0.5};
    dim.dimTextHeight = dim.dimArrowSize = 0.18;
    // No geometry: model view fits the dimension itself (4 wide: text 45 px), not 0 px (D105).
    Drawing only;
    only.prims = {dim};
    QVERIFY(readabilityIssues(only, o).empty());
    // An ordinate dimension has no arrowheads: its style's arrow size says nothing.
    Primitive ord = dim;
    ord.dimType = QStringLiteral("ORDINATE");
    ord.dimArrowSize = 0.9;
    Drawing ordinate;
    ordinate.prims = {ord};
    QVERIFY(readabilityIssues(ordinate, o).empty());
    Primitive lin = ord;
    lin.dimType = QStringLiteral("LINEAR");
    Drawing linear;
    linear.prims = {lin};
    const auto arrows = readabilityIssues(linear, o);
    QCOMPARE(arrows.size(), size_t(1));
    QCOMPARE(arrows[0].label, QStringLiteral("arrows out of proportion"));
    // Nothing spanning any distance: nothing judged, and a note says so.
    Primitive dot = dim;
    dot.p0 = dot.p1 = dot.center = {1, 1};
    Drawing point;
    point.prims = {dot};
    const auto note = readabilityIssues(point, o);
    QCOMPARE(note.size(), size_t(1));
    QVERIFY(note[0].label.isEmpty() && note[0].text.startsWith(QStringLiteral("the drawing spans no distance")));
}

// The detail view's tabs follow each other (D107): a view position taken into the student's file
// keeps its place and the size things show at; Orthographic, the view under it decides.
void TestMisc::viewSyncMapping() {
    // Student -> reference: drawn twice too large, turned 37 degrees and moved.
    const Transform2D T = Transform2D::rigid(deg(37), 3, -2) * Transform2D::scaling(0.5);
    const Camera c{{1.5, 0.75}, 40};  // reference frame, 40 pixels per unit
    const Camera s = mapCamera(c, T.inverse());
    QVERIFY(dist(T.apply(s.center), c.center) < 1e-12);
    QVERIFY(std::fabs(s.scale - 20) < 1e-12);  // a student unit is half a reference unit: half the pixels
    const Camera back = mapCamera(s, T);
    QVERIFY(dist(back.center, c.center) < 1e-12 && std::fabs(back.scale - 40) < 1e-12);
    const Camera moved = mapCamera(c, Transform2D::translation(5, 1));  // only moved: the same zoom
    QVERIFY(dist(moved.center, Vec2{6.5, 1.75}) < 1e-12 && std::fabs(moved.scale - 40) < 1e-12);

    // Orthographic: each view has its own transform.
    CompareResult r;
    r.mode = Mode::Orthographic;
    const Transform2D t0 = Transform2D::translation(-10, 0), t1 = Transform2D::rigid(deg(90), 6, 1);
    r.stuT = {t0, t0, t1};
    r.stuView = {0, 0, 1};
    r.stuPartner = {0, -1, 2};
    ViewSet vs;
    vs.views.resize(2);
    vs.views[0].name = ViewName::Front;
    vs.views[0].bbox.add(Vec2{0, 0});
    vs.views[0].bbox.add(Vec2{2, 2});
    vs.views[1].name = ViewName::Right;
    vs.views[1].bbox.add(Vec2{5, 0});
    vs.views[1].bbox.add(Vec2{7, 2});
    auto same = [](const std::optional<Transform2D>& a, const Transform2D& b) {
        return a && dist(a->apply({0.3, 0.7}), b.apply({0.3, 0.7})) < 1e-12 && dist(a->apply({-2, 5}), b.apply({-2, 5})) < 1e-12;
    };
    QVERIFY(same(refToStudentAt(r, &vs, {6, 1}), t1.inverse()));
    QVERIFY(same(refToStudentAt(r, &vs, {1, 1}), t0.inverse()));
    QVERIFY(same(refToStudentAt(r, &vs, {3, 1}), t0.inverse()));  // between the views: the nearer one
    // The student's file: the view of the student items nearest the point.
    Drawing stu;
    Primitive l;
    l.kind = PrimKind::Line;
    l.p0 = {10, 0};
    l.p1 = {12, 2};
    stu.prims.push_back(l);  // view 0, which t0 moves onto (0, 0)-(2, 2)
    l.p0 = {11, 0};
    l.p1 = {11, 1};
    stu.prims.push_back(l);
    l.p0 = {-1, -6};
    l.p1 = {1, -5};
    stu.prims.push_back(l);  // view 1
    QVERIFY(same(studentToRefAt(r, &vs, stu, {11, 1}), t0));
    QVERIFY(same(studentToRefAt(r, &vs, stu, {0, -5.5}), t1));
    // An Ignored view is located like the others: its own transform (D108)...
    vs.views[1].name = ViewName::Ignored;
    QVERIFY(same(refToStudentAt(r, &vs, {6, 1}), t1.inverse()));
    QVERIFY(same(studentToRefAt(r, &vs, stu, {0, -5.5}), t1));
    // ...and a view with nothing of the student's in it falls back to the nearest that has.
    r.stuView[2] = -1;
    QVERIFY(same(refToStudentAt(r, &vs, {6, 1}), t0.inverse()));
    r.stuView[2] = 1;
    // Another mode: one transform for the whole drawing.
    vs.views[1].name = ViewName::Right;
    r.mode = Mode::Relative;
    QVERIFY(same(refToStudentAt(r, &vs, {6, 1}), t0.inverse()));
    QVERIFY(same(studentToRefAt(r, nullptr, stu, {0, -5.5}), t0));
}

// Creative mode's kinds (D106): shape and linetype, dimensions by type; a centerline is counted
// by its axis, and a center mark's cross not at all.
void TestMisc::creativeKinds() {
    DocBuilder b;
    b.line({0, 0}, {4, 0});
    b.line({4, 0}, {4, 2});
    b.line({0, 0.2}, {4, 0.2}, "0", "HIDDEN");
    b.circle({1, 1}, 0.5);
    b.circle({3, 1}, 0.4, "0", "HIDDEN");
    b.arc({2, 2}, 0.5, 0, 90);
    // A center mark at (1, 1), as AutoCAD draws one: two pieces each way, and a small cross.
    b.line({0.3, 1}, {0.9, 1}, "0", "CENTER");
    b.line({1.1, 1}, {1.7, 1}, "0", "CENTER");
    b.line({1, 0.3}, {1, 0.9}, "0", "CENTER");
    b.line({1, 1.1}, {1, 1.7}, "0", "CENTER");
    b.line({0.95, 1}, {1.05, 1});
    b.line({1, 0.95}, {1, 1.05});
    b.dimension("LINEAR", 4, {0, 0}, {4, 0});
    b.dimension("ALIGNED", 2, {4, 0}, {4, 2});
    const Drawing d = b.canon();
    CompareSettings s;
    auto kinds = [&]() {
        QStringList got;
        for (const CreativeItems& it : creativeItems(d, s)) got << countOf(it.count, it.kind);
        return got.join(QStringLiteral(", "));
    };
    QCOMPARE(kinds(), QStringLiteral("2 visible lines, 1 hidden line, 2 centerlines, 1 arc, 1 circle, 1 hidden circle, "
                                     "1 linear dimension, 1 aligned dimension"));
    auto needs = [&](const CreativeChoices& ch) {
        QStringList got;
        for (const CreativeNeed& n : creativeNeeds(creativeItems(d, s), ch))
            got << countOf(n.need, n.key, n.plural) + (n.merged ? QStringLiteral(" (merged)") : QString());
        return got.join(QStringLiteral(", "));
    };
    QCOMPARE(needs({}), QStringLiteral("2 visible lines, 1 hidden line, 2 centerlines, 1 arc, 1 circle, 1 hidden circle, "
                                       "1 linear dimension, 1 aligned dimension"));
    // Linetypes need not match: the kinds are still listed apart, for the choices, and graded
    // together, each line kind's count added (D108).
    s.matchLinetype = false;
    QCOMPARE(kinds(), QStringLiteral("2 visible lines, 1 hidden line, 2 centerlines, 1 arc, 1 circle, 1 hidden circle, "
                                     "1 linear dimension, 1 aligned dimension"));
    QCOMPARE(needs({}), QStringLiteral("5 lines (merged), 1 arc, 2 circles (merged), 1 linear dimension, 1 aligned dimension"));
    CreativeChoices ch;
    ch.off << QStringLiteral("hidden line") << QStringLiteral("hidden circle");
    ch.counts[QStringLiteral("centerline")] = 1;
    QCOMPARE(needs(ch), QStringLiteral("3 lines (merged), 1 arc, 1 circle, 1 linear dimension, 1 aligned dimension"));
    QCOMPARE(pluralKind(QStringLiteral("hatch (ANSI31)")), QStringLiteral("hatches (ANSI31)"));
    QCOMPARE(pluralKind(QStringLiteral("elliptical arc")), QStringLiteral("elliptical arcs"));
    QCOMPARE(pluralKind(QStringLiteral("arc length dimension")), QStringLiteral("arc length dimensions"));
}

// Creative mode's grading (D106): each kind as many as required, anywhere and at any size; an
// item that breaks a rule its kind follows in the solution doesn't count.
void TestMisc::creativeGrading() {
    DocBuilder rb;  // the solution: two hidden lines on HIDDEN, three circles, a linear dimension
    rb.layer("HIDDEN", "HIDDEN");
    rb.line({0, 0}, {6, 0}, "HIDDEN", "HIDDEN");
    rb.line({0, 1}, {6, 1}, "HIDDEN", "HIDDEN");
    for (int k = 0; k < 3; ++k) rb.circle({1.0 + 2 * k, 3}, 0.5);
    rb.dimension("LINEAR", 6, {0, 0}, {6, 0});
    const Drawing ref = rb.canon();
    DocBuilder sb;  // the student's own design: larger, elsewhere, one hidden line on layer 0, four circles
    sb.layer("HIDDEN", "HIDDEN");
    sb.line({10, 10}, {30, 10}, "HIDDEN", "HIDDEN");
    sb.line({10, 14}, {30, 14}, "0", "HIDDEN");
    for (int k = 0; k < 4; ++k) sb.circle({12.0 + 5 * k, 20}, 1.5);
    sb.dimension("LINEAR", 20, {10, 10}, {30, 10});
    const Drawing stu = sb.canon();
    CompareSettings s;
    s.mode = Mode::Creative;
    CompareResult r = compareDrawings(ref, stu, s, nullptr);
    QVERIFY2(r.pass && r.correctness == 100, qPrintable(r.errorStrings().join(" | ")));
    QCOMPARE(r.refCount, 6);
    QCOMPARE(r.penalizedExtras, 0);  // the fourth circle
    // Layer names must match: all of the solution's hidden lines are on HIDDEN, so a student's must be.
    s.requireLayerMatch = true;
    r = compareDrawings(ref, stu, s, nullptr);
    QCOMPARE(r.matched, 5);
    QVERIFY(std::fabs(r.correctness - 100.0 * 5 / 6) < 1e-9);
    QCOMPARE(r.errors.size(), size_t(1));
    QCOMPARE(r.errors[0].text, QStringLiteral("1 of 2 hidden lines (1 missing); 1 more hidden line doesn't count: layer 0, expected HIDDEN"));
    QCOMPARE(r.errors[0].label, QStringLiteral("too few hidden lines"));
    QCOMPARE(r.errors[0].refPrims.size(), size_t(2));
    QCOMPARE(r.errors[0].stuPrims.size(), size_t(2));
    // Fewer needed than the solution has: one is enough, and the other is only noted.
    s.creative.counts[QStringLiteral("hidden line")] = 1;
    r = compareDrawings(ref, stu, s, nullptr);
    QVERIFY2(r.pass, qPrintable(r.errorStrings().join(" | ")));
    QCOMPARE(r.refCount, 5);
    QCOMPARE(r.errors.size(), size_t(1));
    QVERIFY(!r.errors[0].failing && r.errors[0].text.contains(QStringLiteral("doesn't count")));
    // More circles than the student drew, and the dimension not checked.
    s.creative.counts[QStringLiteral("circle")] = 5;
    s.creative.off << QStringLiteral("linear dimension");
    r = compareDrawings(ref, stu, s, nullptr);
    QCOMPARE(r.refCount, 6);
    QCOMPARE(r.matched, 5);
    QVERIFY2(r.errorStrings().contains(QStringLiteral("4 of 5 circles (1 missing)")), qPrintable(r.errorStrings().join(" | ")));
    // Each item's state, for the detail view: the student's are counted, not needed or don't count.
    int counted = 0, notCounted = 0;
    for (PrimStatus st : r.stuStatus) {
        counted += st == PrimStatus::Matched;
        notCounted += st == PrimStatus::WrongAttribute;
    }
    QCOMPARE(counted, 5);
    QCOMPARE(notCounted, 1);

    // Choices made with the linetypes apart still apply with "Linetypes must match" off: then a
    // student's visible lines count for the solution's hidden ones (D108).
    DocBuilder vb;
    vb.line({0, 0}, {9, 0});
    vb.line({0, 2}, {9, 2});
    for (int k = 0; k < 3; ++k) vb.circle({1.0 + 3 * k, 5}, 1);
    vb.dimension("LINEAR", 9, {0, 0}, {9, 0});
    const Drawing visible = vb.canon();
    CompareSettings m;
    m.mode = Mode::Creative;
    r = compareDrawings(ref, visible, m, nullptr);
    QVERIFY2(r.errorStrings().contains(QStringLiteral("0 of 2 hidden lines (2 missing)")), qPrintable(r.errorStrings().join(" | ")));
    m.matchLinetype = false;
    m.creative.counts[QStringLiteral("hidden line")] = 1;
    r = compareDrawings(ref, visible, m, nullptr);
    QVERIFY2(r.pass, qPrintable(r.errorStrings().join(" | ")));
    QCOMPARE(r.refCount, 5);  // 1 line, 3 circles, 1 dimension
}

// The centerline rule (D106).
void TestMisc::creativeCenterlineRule() {
    DocBuilder b;
    b.circle({0, 0}, 1);  // centerlines drawn as lines, past it both ways: fine
    b.line({-1.2, 0}, {1.2, 0}, "0", "CENTER");
    b.line({0, -1.2}, {0, 1.2}, "0", "CENTER");
    b.circle({5, 0}, 1);  // as AutoCAD's center mark draws them: a gap at the center, and a cross
    b.line({3.8, 0}, {4.9, 0}, "0", "CENTER");
    b.line({5.1, 0}, {6.2, 0}, "0", "CENTER");
    b.line({5, -1.2}, {5, -0.1}, "0", "CENTER");
    b.line({5, 0.1}, {5, 1.2}, "0", "CENTER");
    b.line({4.95, 0}, {5.05, 0});
    b.line({5, -0.05}, {5, 0.05});
    b.circle({10, 0}, 1);  // none: another hole's centerline along the same axis isn't its own
    b.circle({15, 0}, 1);  // one way only
    b.line({13.8, 0}, {16.2, 0}, "0", "CENTER");
    b.circle({20, 5}, 1);  // too short
    b.line({19.5, 5}, {20.5, 5}, "0", "CENTER");
    b.line({20, 4.5}, {20, 5.5}, "0", "CENTER");
    b.arc({25, 0}, 1, 0, 180);              // a half circle needs them
    b.arc({30, 0}, 1, 0, 90);               // a fillet doesn't
    b.circle({35, 0}, 1, "0", "CENTER");    // nor a bolt circle
    // Holes on a bolt circle: a radial centerline and the bolt circle itself are its two (D108).
    b.circle({50, 0}, 3, "0", "CENTER");
    b.circle({53, 0}, 0.3);                       // fine
    b.line({52.4, 0}, {53.6, 0}, "0", "CENTER");
    b.circle({50, 3}, 0.3);                       // the bolt circle alone: one direction only
    const Vec2 u5 = fromAngle(deg(5));            // on an arc of 10 degrees: it stops 0.26 past the
    b.arc({60, 0}, 3, 0, 10, "0", "CENTER");      // center, short of the 0.3 hole's side
    b.circle(Vec2{60, 0} + u5 * 3, 0.3);
    b.line(Vec2{60, 0} + u5 * 2.4, Vec2{60, 0} + u5 * 3.6, "0", "CENTER");
    const Drawing d = b.canon();
    const std::vector<CenterlineProblem> p = centerlineProblems(d, 1e-6);
    auto at = [&](Vec2 c) {
        for (const CenterlineProblem& x : p)
            if (dist(d.prims[x.prim].center, c) < 1e-9) return x.text;
        return QString();
    };
    QCOMPARE(p.size(), size_t(6));
    QVERIFY2(at({53, 0}).isEmpty(), qPrintable(at({53, 0})));
    QVERIFY2(at({50, 3}).endsWith(QStringLiteral("has a centerline in one direction only")), qPrintable(at({50, 3})));
    const Vec2 c5 = Vec2{60, 0} + u5 * 3;
    QVERIFY2(at(c5).endsWith(QStringLiteral("its centerlines don't reach past it on both sides")), qPrintable(at(c5)));
    QVERIFY2(at({10, 0}).endsWith(QStringLiteral("has no centerlines")), qPrintable(at({10, 0})));
    QVERIFY2(at({15, 0}).endsWith(QStringLiteral("has a centerline in one direction only")), qPrintable(at({15, 0})));
    QVERIFY2(at({20, 5}).endsWith(QStringLiteral("its centerlines don't reach past it on both sides")), qPrintable(at({20, 5})));
    QVERIFY2(at({25, 0}).endsWith(QStringLiteral("has no centerlines")), qPrintable(at({25, 0})));
}

// The corner rule (D106).
void TestMisc::creativeCornerRule() {
    DocBuilder b;
    // A 10 x 5 outline: three corners meet, one has a gap of 0.02, and one line runs 0.03 past another.
    b.line({0, 0}, {10, 0});
    b.line({10, 0}, {10, 5});
    b.line({9.98, 5}, {0, 5});
    b.line({0, 5}, {0, -0.03});
    b.line({5, 0}, {5, 2}, "0", "HIDDEN");     // a T: touches the bottom; its top end is open
    b.line({2, 5}, {2, 3});                    // a visible line, continued by a hidden one after a
    b.line({2, 2.97}, {2, 1}, "0", "HIDDEN");  // gap: the drafting convention
    b.line({20, 20}, {25, 20});                // far from everything: open ends
    b.circle({7, 2}, 1);                       // a hand-drawn center mark's cross is not an edge
    b.line({6.95, 2}, {7.05, 2});
    b.line({7, 1.95}, {7, 2.05});
    const Drawing d = b.canon();
    const std::vector<CornerProblem> c = cornerProblems(d, 1e-6);
    QCOMPARE(c.size(), size_t(2));
    const CornerProblem& gap = c[0].overshoot ? c[1] : c[0];
    const CornerProblem& over = c[0].overshoot ? c[0] : c[1];
    QVERIFY(!gap.overshoot && over.overshoot);
    QVERIFY(dist(gap.at, Vec2{9.99, 5}) < 1e-9);
    QVERIFY(std::fabs(gap.worst - 0.02) < 1e-9);
    QCOMPARE(gap.prims.size(), size_t(2));
    QVERIFY(dist(over.at, Vec2{0, -0.03}) < 1e-9);
    QVERIFY(std::fabs(over.worst - 0.03) < 1e-9);
    QCOMPARE(countCorners(d, 1e-6), 2);  // where ends meet ends: (10, 0) and (0, 5)
    // Distances to curves, for what touches: on the circle, near the arc's end.
    Primitive arc;
    arc.kind = PrimKind::Arc;
    arc.center = {0, 0};
    arc.radius = 2;
    arc.a0 = 0;
    arc.a1 = kPi / 2;
    QVERIFY(std::fabs(distanceToPrimitive(arc, {0, 3}) - 1) < 1e-12);
    QVERIFY(std::fabs(distanceToPrimitive(arc, {3, -1}) - std::hypot(1, 1)) < 1e-12);  // past its start: to the end point
    Primitive ell;
    ell.kind = PrimKind::Ellipse;
    ell.center = {0, 0};
    ell.radius = 3;
    ell.minorRadius = 1;
    QVERIFY(distanceToPrimitive(ell, ell.pointAt(1.1)) < 1e-9);
    QVERIFY(std::fabs(distanceToPrimitive(ell, {0, 2.5}) - 1.5) < 1e-9);

    // A stub hanging off an edge is near it only because it is attached there: an open end. One
    // that stops short of another line is a gap to that line (D108).
    DocBuilder sb;
    sb.line({0, 0}, {10, 0});
    sb.line({0, 1}, {10, 1});
    sb.line({5, 0}, {5, 0.05});
    sb.line({8, 0}, {8, 0.99});
    const Drawing stubs = sb.canon();
    const std::vector<CornerProblem> st = cornerProblems(stubs, 1e-6);
    QCOMPARE(st.size(), size_t(1));
    QVERIFY(!st[0].overshoot && std::fabs(st[0].worst - 0.01) < 1e-9 && dist(st[0].at, Vec2{8, 0.99}) < 1e-9);
    // A spline snapped closed onto its start, without AutoCAD's closed flag, has no ends.
    Drawing snapped;
    snapped.prims.push_back(spline({{0, 0}, {2, 1}, {4, 0}, {2, -1}, {1e-9, 0}}));
    Primitive ring;
    ring.kind = PrimKind::Circle;
    ring.center = {-0.05, 0};
    ring.radius = 0.03;
    snapped.prims.push_back(ring);
    QVERIFY(cornerProblems(snapped, 1e-6).empty());
    QCOMPARE(countCorners(snapped, 1e-6), 0);
}

// Creative mode's choices are saved per solution drawing, and reach its comparison (D106).
void TestMisc::creativeChoicesSaved() {
    AppSettings a;
    CreativeChoices ch;
    ch.off << QStringLiteral("hidden line");
    ch.counts[QStringLiteral("circle")] = 3;
    ch.centerlines = false;
    a.creative[QStringLiteral("plate.dwg")] = ch;
    a.creative[QStringLiteral("part b.dwg")] = CreativeChoices{};
    const AppSettings b = AppSettings::fromJson(a.toJson());
    QCOMPARE(b.creative.size(), size_t(2));
    QVERIFY(b.creative.at(QStringLiteral("plate.dwg")) == ch);
    QVERIFY(b.creativeFor(QStringLiteral("C:/x/Plate.DWG")) == ch);
    QVERIFY(b.compareFor(QStringLiteral("C:/x/Plate.dwg")).creative == ch);
    QVERIFY(b.creativeFor(QStringLiteral("other.dwg")) == CreativeChoices{});
    Mode m = Mode::Absolute;
    QVERIFY(parseMode(QStringLiteral("Creative"), &m) && m == Mode::Creative);
    QCOMPARE(modeName(Mode::Creative), QStringLiteral("creative"));
}

void TestMisc::lineMatchEitherOrder() {
    MatchCriteria c;
    QVERIFY(geometryMatches(line({0, 0}, {1, 0}), line({1, 0}, {0, 0}), c));
    QVERIFY(geometryMatches(line({0, 0}, {1, 0}), line({0.00005, 0}, {1, 0.00005}), c));
    QVERIFY(!geometryMatches(line({0, 0}, {1, 0}), line({0.0002, 0}, {1, 0}), c));
}

void TestMisc::arcMatchAngles() {
    MatchCriteria c;
    QVERIFY(geometryMatches(arc({0, 0}, 1, 0, 1), arc({0, 0}, 1, 0, 1), c));
    QVERIFY(!geometryMatches(arc({0, 0}, 1, 0, 1), arc({0, 0}, 1, 0, 1.01), c));
    QVERIFY(!geometryMatches(arc({0, 0}, 1, 0, 1), arc({0, 0}, 1.001, 0, 1), c));
}

void TestMisc::looseCenterlines() {
    MatchCriteria c;
    const Primitive ref = line({0, 0}, {2, 0}, LtClass::Center);
    QVERIFY(geometryMatches(ref, line({-0.3, 0}, {2.4, 0}, LtClass::Center), c));  // different extension
    QVERIFY(!geometryMatches(ref, line({0, 0.01}, {2, 0.01}, LtClass::Center), c));  // moved
    QVERIFY(!geometryMatches(ref, line({3, 0}, {4, 0}, LtClass::Center), c));        // no overlap
    c.strictCenterlines = true;
    QVERIFY(!geometryMatches(ref, line({-0.3, 0}, {2.4, 0}, LtClass::Center), c));
}

void TestMisc::greedyOneToOne() {
    std::vector<Primitive> ref{line({0, 0}, {1, 0}), line({0, 0}, {1, 0})};
    std::vector<Primitive> stu{line({0, 0}, {1, 0})};
    Matching m;
    m.reset(ref.size(), stu.size());
    matchInto(ref, all(ref), stu, all(stu), MatchCriteria{}, false, m);
    QCOMPARE(m.matched, 1);
}

void TestMisc::nearMissPairing() {
    std::vector<Primitive> ref{circle({1, 1}, 0.375), line({0, 0}, {2.5, 0})};
    std::vector<Primitive> stu{line({0, 0}, {2.375, 0}), circle({1.125, 1}, 0.375)};
    const auto pairs = pairNearMisses(ref, all(ref), stu, all(stu), 0.5, MatchCriteria{});
    QCOMPARE(pairs.size(), size_t(2));
    for (const auto& p : pairs) QCOMPARE(ref[p.ref].kind, stu[p.stu].kind);
}

void TestMisc::supersetLine() {
    std::vector<Primitive> ref{line({0, 0}, {0, 1.5}), line({0, 2.5}, {0, 4.5})};
    std::vector<Primitive> stu{line({0, 0}, {0, 4.5})};
    const auto pairs = pairNearMisses(ref, all(ref), stu, all(stu), 0.5, MatchCriteria{});
    QCOMPARE(pairs.size(), size_t(2));
    for (const auto& p : pairs) QVERIFY(p.superset);
}

void TestMisc::alignRecoversRigidTransform() {
    std::vector<Primitive> ref{line({0, 0}, {4, 0}), line({4, 0}, {4, 1.5}), line({0, 2}, {0, 0}), circle({1, 1}, 0.375),
                               arc({3.5, 1.5}, 0.5, 0, kPi / 2)};
    const Transform2D move = Transform2D::rigid(deg(-123), 17.25, -3.5);
    std::vector<Primitive> stu;
    for (const auto& p : ref) stu.push_back(transformed(p, move));
    AlignOptions o;
    const AlignResult r = alignDrawings(ref, all(ref), stu, all(stu), o);
    QCOMPARE(r.best.score, 5);
    const Transform2D inv = move.inverse();
    QVERIFY(std::fabs(r.best.T.tx - inv.tx) < 1e-9);
    QVERIFY(std::fabs(r.best.T.ty - inv.ty) < 1e-9);
    QVERIFY(angleDiff(r.best.T.rotationAngle(), inv.rotationAngle()) < 1e-9);
}

void TestMisc::alignCircleOnlyDrawing() {
    std::vector<Primitive> ref{circle({0, 0}, 1), circle({5, 0}, 0.5), circle({0, 3}, 0.25)};
    const Transform2D move = Transform2D::rigid(deg(70), 2, 2);
    std::vector<Primitive> stu;
    for (const auto& p : ref) stu.push_back(transformed(p, move));
    const AlignResult r = alignDrawings(ref, all(ref), stu, all(stu), AlignOptions{});
    QCOMPARE(r.best.score, 3);  // a second anchor fixes the rotation
}

void TestMisc::alignMirror() {
    std::vector<Primitive> ref{line({0, 0}, {4, 0}), line({4, 0}, {4, 1.5}), circle({1, 1}, 0.375), arc({3.5, 1.5}, 0.5, 0, kPi / 2)};
    std::vector<Primitive> stu;
    for (const auto& p : ref) stu.push_back(transformed(p, Transform2D::mirrorX()));
    AlignOptions o;
    QVERIFY(alignDrawings(ref, all(ref), stu, all(stu), o).best.score < 4);
    o.allowMirror = true;
    const AlignResult r = alignDrawings(ref, all(ref), stu, all(stu), o);
    QCOMPARE(r.best.score, 4);
    QVERIFY(r.best.mirrored);
}

void TestMisc::alignEllipseOnlyDrawing() {
    // Nothing but ellipses, as in an isometric drawing's holes: their axes fix the turn.
    std::vector<Primitive> ref{ellipse({0, 0}, {1.2247, 0.7071}, Vec2{-0.7071, 1.2247} * 0.5),
                               ellipse({4, 1}, {0, 1}, {-0.6, 0}, 0, kPi, false)};
    const Transform2D move = Transform2D::rigid(deg(70), 2, -3);
    std::vector<Primitive> stu;
    for (const auto& p : ref) stu.push_back(transformed(p, move));
    const AlignResult r = alignDrawings(ref, all(ref), stu, all(stu), AlignOptions{});
    QCOMPARE(r.best.score, 2);
    const Transform2D inv = move.inverse();
    QVERIFY(std::fabs(r.best.T.tx - inv.tx) < 1e-9 && std::fabs(r.best.T.ty - inv.ty) < 1e-9);
    // A whole ellipse alone: turned to either end of its axis (both fit).
    std::vector<Primitive> one{ref[0]}, oneStu{stu[0]};
    QCOMPARE(alignDrawings(one, all(one), oneStu, all(oneStu), AlignOptions{}).best.score, 1);
}

void TestMisc::alignSplineOnlyDrawing() {
    // Nothing but splines: an open one's ends fix the turn; a closed one only counts.
    std::vector<Primitive> ref{spline({{0, 0}, {1, 1}, {2, 0.5}, {3, 1.5}, {4, 0}}),
                               spline({{1, -2}, {2, -1.4}, {3, -1.8}, {2.2, -2.4}}, true)};
    const Transform2D move = Transform2D::rigid(deg(70), 2, -3);
    std::vector<Primitive> stu;
    for (const auto& p : ref) stu.push_back(transformed(p, move));
    const AlignResult r = alignDrawings(ref, all(ref), stu, all(stu), AlignOptions{});
    QCOMPARE(r.best.score, 2);
    const Transform2D inv = move.inverse();
    QVERIFY(std::fabs(r.best.T.tx - inv.tx) < 1e-9 && std::fabs(r.best.T.ty - inv.ty) < 1e-9);
}

void TestMisc::identityCanvas() {
    const IdentityParser p(AppSettings::defaultIdentityRegex());
    const StudentIdentity a = p.parse("C:/subs/smithjane_12345_67890_lab3.dwg");
    QVERIFY(a.parsed);
    QCOMPARE(a.name, QStringLiteral("smithjane"));
    QCOMPARE(a.uid, QStringLiteral("12345"));
    QCOMPARE(a.sid, QStringLiteral("67890"));
    QCOMPARE(a.orig, QStringLiteral("lab3.dwg"));
    const StudentIdentity b = p.parse("doejohn_LATE_111_222_Lab 3-1.dwg");
    QVERIFY(b.parsed);
    QVERIFY(b.late);
    QCOMPARE(b.name, QStringLiteral("doejohn"));
    QCOMPARE(b.orig, QStringLiteral("Lab 3-1.dwg"));
}

void TestMisc::identityFallback() {
    const IdentityParser p(AppSettings::defaultIdentityRegex());
    const StudentIdentity a = p.parse("C:/subs/My Drawing.dwg");
    QVERIFY(!a.parsed);
    QCOMPARE(a.name, QStringLiteral("My Drawing"));
}

// A Canvas name with accented letters is still a name, and one student's files group
// together whether an accent is one character or a letter plus a mark (D80).
void TestMisc::identityAccentedNames() {
    const IdentityParser p(AppSettings::defaultIdentityRegex());
    const StudentIdentity a = p.parse(QStringLiteral(u"C:/subs/garc\u00EDajos\u00E9_123_456_part1.dwg"));
    const StudentIdentity b = p.parse(QStringLiteral(u"garci\u0301ajose\u0301_LATE_123_457_part2.dwg"));
    QVERIFY(a.parsed && b.parsed);
    QCOMPARE(a.name, QStringLiteral(u"garc\u00EDajos\u00E9"));
    QCOMPARE(b.name, a.name);
    QCOMPARE(a.uid, QStringLiteral("123"));
    QCOMPARE(studentKey(a), studentKey(b));
    QCOMPARE(p.parse(QStringLiteral(u"\u00F8stergaardm\u00E5ns_9_10_x.dwg")).name, QStringLiteral(u"\u00F8stergaardm\u00E5ns"));
    // Another student's files stay apart.
    QVERIFY(studentKey(p.parse(QStringLiteral(u"garc\u00EDajos\u00E9_124_458_part1.dwg"))) != studentKey(a));

    // Settings saved with the old a-z default get the new one; a pattern of the user's own stays.
    const QString oldDefault = QStringLiteral(R"(^(?<name>[a-z]+)_(?:LATE_)?(?<uid>\d+)_(?<sid>\d+)_(?<orig>.+)$)");
    AppSettings s;
    s.identityRegex = oldDefault;
    QCOMPARE(AppSettings::fromJson(s.toJson()).identityRegex, AppSettings::defaultIdentityRegex());
    s.identityRegex = QStringLiteral("^(?<name>[a-z]+)-");
    QCOMPARE(AppSettings::fromJson(s.toJson()).identityRegex, s.identityRegex);
}

// A zip of submissions written by Python's zip writer (fixtures/make_submissions_zip.py):
// drawings from any folder, names in UTF-8 or in code page 437, Mac clutter and other files
// left out, nothing written outside the folder, and a damaged file reported (D81).
void TestMisc::zipSubmissions() {
    const QString zip = QStringLiteral(DWGCHECK_FIXTURES "/submissions.zip");
    const ZipListing list = listZipSubmissions(zip);
    QVERIFY2(list.error.isEmpty(), qPrintable(list.error));
    QCOMPARE(list.nestedZips, 1);
    const QStringList names = {QStringLiteral("../evil_1_2_x.dwg"),
                               QStringLiteral("broken_5_6_b.dwg"),
                               QStringLiteral("dup/a_1_2_x.dwg"),
                               QStringLiteral("dup/A_1_2_X.dwg"),
                               QStringLiteral(u"garc\u00EDajos\u00E9_123_456_part1.dwg"),
                               QStringLiteral(u"mu\u00F1ozcarlos_7_8_a.dwg"),
                               QStringLiteral("smithjane_12345_67890_lab3.dwg"),
                               QStringLiteral("sub/doejohn_111_222_x.DWG")};
    QCOMPARE(list.names, names);

    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString dest = tmp.filePath(QStringLiteral("unpacked"));
    int lastDone = -1, lastTotal = -1;
    const ZipUnpacked u = unpackZipSubmissions(zip, dest, nullptr, [&](int done, int total) {
        lastDone = done;
        lastTotal = total;
    });
    QVERIFY2(u.error.isEmpty(), qPrintable(u.error));
    QVERIFY(!u.cancelled);
    QCOMPARE(lastDone, 8);
    QCOMPARE(lastTotal, 8);
    QCOMPARE(u.problems.size(), 1);  // the damaged one
    QVERIFY2(u.problems.front().startsWith(QStringLiteral("broken_5_6_b.dwg: ")) && u.problems.front().contains(QStringLiteral("checksum")),
             qPrintable(u.problems.front()));
    QCOMPARE(u.files.size(), 7);
    QCOMPARE(u.files.front(), dest + QStringLiteral("/evil_1_2_x.dwg"));
    auto body = [](const char* tag) { return (QByteArray("AC1032 ") + tag + ' ').repeated(20); };
    auto contents = [&](const QString& rel) {
        QFile f(dest + QLatin1Char('/') + rel);
        return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray("(missing)");
    };
    QCOMPARE(contents(QStringLiteral("smithjane_12345_67890_lab3.dwg")), body("smith"));
    QCOMPARE(contents(QStringLiteral(u"garc\u00EDajos\u00E9_123_456_part1.dwg")), body("garcia"));
    QCOMPARE(contents(QStringLiteral(u"mu\u00F1ozcarlos_7_8_a.dwg")), body("munoz"));  // stored, not packed
    QCOMPARE(contents(QStringLiteral("sub/doejohn_111_222_x.DWG")), body("doe"));
    QCOMPARE(contents(QStringLiteral("evil_1_2_x.dwg")), body("evil"));  // its "../" dropped
    QVERIFY(!QFileInfo::exists(tmp.filePath(QStringLiteral("evil_1_2_x.dwg"))));
    QCOMPARE(contents(QStringLiteral("dup/a_1_2_x.dwg")), body("lower"));
    QCOMPARE(contents(QStringLiteral("dup/A_1_2_X (2).dwg")), body("upper"));  // not written over the first
    QVERIFY(!QFileInfo::exists(dest + QStringLiteral("/__MACOSX")));
    QVERIFY(!QFileInfo::exists(dest + QStringLiteral("/readme.txt")));
    QVERIFY(!QFileInfo::exists(dest + QStringLiteral("/broken_5_6_b.dwg")));
    // The unpacked names read as the students' (accents and all).
    const IdentityParser p(AppSettings::defaultIdentityRegex());
    QCOMPARE(p.parse(u.files.at(3)).name, QStringLiteral(u"garc\u00EDajos\u00E9"));
    QCOMPARE(p.parse(u.files.at(4)).name, QStringLiteral(u"mu\u00F1ozcarlos"));

    // Cancelled before it starts: nothing written.
    const std::atomic_bool stop{true};
    const ZipUnpacked none = unpackZipSubmissions(zip, tmp.filePath(QStringLiteral("cancelled")), &stop);
    QVERIFY(none.cancelled && none.files.isEmpty());
}

void TestMisc::zipThatCannotBeRead() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    auto write = [&](const QString& name, const QByteArray& bytes) {
        const QString path = tmp.filePath(name);
        QFile f(path);
        if (f.open(QIODevice::WriteOnly)) f.write(bytes);
        return path;
    };
    QFile fixture(QStringLiteral(DWGCHECK_FIXTURES "/submissions.zip"));
    QVERIFY(fixture.open(QIODevice::ReadOnly));
    const QByteArray zip = fixture.readAll();
    const ZipListing text = listZipSubmissions(write(QStringLiteral("text.zip"), "not a zip at all"));
    QVERIFY(text.error.contains(QStringLiteral("not a zip")));
    QVERIFY(text.names.isEmpty());
    QVERIFY(!listZipSubmissions(write(QStringLiteral("cut.zip"), zip.left(1000))).error.isEmpty());    // end missing
    QVERIFY(!listZipSubmissions(write(QStringLiteral("shifted.zip"), zip.mid(100))).error.isEmpty());  // start missing
    QVERIFY(!listZipSubmissions(tmp.filePath(QStringLiteral("absent.zip"))).error.isEmpty());
    QVERIFY(!unpackZipSubmissions(tmp.filePath(QStringLiteral("text.zip")), tmp.filePath(QStringLiteral("out"))).error.isEmpty());
    QVERIFY(isZipPath(QStringLiteral("C:/x/Submissions.ZIP")) && !isZipPath(QStringLiteral("C:/x/zip")));
}

// The Class-wide summary (D89): scores, kinds of error by students and times, and the parts
// of the solution by how many students missed them and how.
void TestMisc::classSummaryCounts() {
    auto ref = std::make_shared<Drawing>();
    for (int i = 0; i < 4; ++i) {
        Primitive p = line({0, double(i)}, {2, double(i)});
        p.layer = QStringLiteral("VISIBLE");
        ref->prims.push_back(p);
    }
    BatchOutput b;
    b.refs = {ref};
    b.refPaths = {QStringLiteral("q.dwg")};
    b.refModes = {Mode::Relative};
    using S = PrimStatus;
    auto add = [&](std::vector<PrimStatus> st, std::vector<ErrorItem> errors, double correctness) {
        StudentRun r;
        r.drawing = ref;
        r.refIndex = 0;
        r.result.refStatus = std::move(st);
        r.result.errors = std::move(errors);
        r.result.correctness = correctness;
        b.students.push_back(r);
        StudentGroup g;
        g.files = {static_cast<int>(b.students.size()) - 1};
        g.byRef = g.files;
        b.groups.push_back(g);
    };
    auto err = [](const char* label, std::vector<int> refPrims) {
        ErrorItem e;
        e.cat = ErrCat::Missing;
        e.label = QString::fromLatin1(label);
        e.refPrims = std::move(refPrims);
        e.failing = true;
        return e;
    };
    // Prim 3 is graded for no one (say, in an Ignored view).
    add({S::Matched, S::Matched, S::Matched, S::NotCompared}, {}, 100);
    add({S::Missing, S::NearMiss, S::Matched, S::NotCompared}, {err("missing", {0}), err("shifted", {1}), err("extra", {})}, 40);
    ErrorItem warning = err("", {2});
    warning.failing = false;  // warnings are not errors
    add({S::Missing, S::Matched, S::Matched, S::NotCompared}, {err("missing", {0}), warning}, 70);
    StudentGroup none;  // a student with no file for the drawing
    none.byRef = {-1};
    b.groups.push_back(none);

    const DrawingSummary s = summarizeDrawing(b, 0);
    QCOMPARE(s.graded, 3);
    QCOMPARE(s.noFile, 1);
    QCOMPARE(s.scores, (std::vector<double>{40, 70, 100}));
    QCOMPARE(s.mean, 70.0);
    QCOMPARE(s.median, 70.0);
    QCOMPARE(s.withErrors, 2);
    QCOMPARE(s.errorCount, 4);
    QCOMPARE(s.kinds.size(), size_t(3));
    QCOMPARE(s.kinds[0].label, QStringLiteral("missing"));
    QCOMPARE(s.kinds[0].students, 2);
    QCOMPARE(s.kinds[0].times, 2);
    QVERIFY(s.kinds[0].onSolution);
    QCOMPARE(s.kinds[1].label, QStringLiteral("shifted"));  // a tie with "extra": the results' order
    QCOMPARE(s.kinds[2].label, QStringLiteral("extra"));
    QVERIFY(!s.kinds[2].onSolution);  // the students' own items
    QCOMPARE(s.parts.size(), size_t(3));  // prim 3 was graded for no one
    QCOMPARE(s.parts[0].prim, 0);
    QCOMPARE(s.parts[0].missed, 2);
    QCOMPARE(s.parts[0].compared, 3);
    QCOMPARE(s.parts[0].count(QStringLiteral("missing")), 2);
    QCOMPARE(describeHow(s.parts[0]), QStringLiteral("2 missing"));
    QCOMPARE(s.parts[1].prim, 1);
    QCOMPARE(s.parts[1].missed, 1);
    QCOMPARE(s.parts[2].prim, 2);
    QCOMPARE(s.parts[2].missed, 0);  // the warning on it is not an error
    QCOMPARE(s.partOf, (std::vector<int>{0, 1, 2, -1}));
    QCOMPARE(describePart(b, 0, 0), QStringLiteral("line 2.000 long on VISIBLE"));
    {  // ellipses (D98)
        BatchOutput e;
        auto d = std::make_shared<Drawing>();
        Primitive p = ellipse({1, 1}, {1, 0}, {0, 0.5});
        p.layer = QStringLiteral("VISIBLE");
        d->prims.push_back(p);
        p = ellipse({1, 1}, {1, 0}, {0, 0.5775}, 0, kPi, false);
        p.layer = QStringLiteral("VISIBLE");
        d->prims.push_back(p);
        e.refs.push_back(d);
        QCOMPARE(describePart(e, 0, 0), QStringLiteral("ellipse 2.000 \u00D7 1.000 on VISIBLE"));
        QCOMPARE(describePart(e, 0, 1), QStringLiteral("elliptical arc 2.000 \u00D7 1.155 on VISIBLE"));
        p = spline({{0, 0}, {1, 1}, {2, 0.5}, {3, 1.5}, {4, 0}});
        p.layer = QStringLiteral("VISIBLE");
        d->prims.push_back(p);
        QCOMPARE(describePart(e, 0, 2), QStringLiteral("spline through 5 points on VISIBLE"));
    }

    // All the drawings: here one, so each student's overall score is that drawing's (D90).
    b.groups[0].correctness = 100;
    b.groups[1].correctness = 40;
    b.groups[2].correctness = 70;
    const ClassOverall o = summarizeClass(b);
    QCOMPARE(o.students, 4);
    QCOMPARE(o.complete, 3);  // the fourth has no file
    QCOMPARE(o.scores, (std::vector<double>{0, 40, 70, 100}));
    QCOMPARE(o.median, 55.0);

    const QString csv = classSummaryCsv(b);
    QVERIFY2(csv.contains(QStringLiteral("\nmissing,2,2\n")), qPrintable(csv));
    QVERIFY2(csv.contains(QStringLiteral("\nline 2.000 long on VISIBLE,2,3,2 missing\n")), qPrintable(csv));
    QVERIFY(!csv.contains(QStringLiteral("line 2.000 long on VISIBLE,0,")));  // parts no one missed are left out
}

// The summary's colors (D90, D92): green for none, up to yellow at the most common share at
// or under half, red past half.
void TestMisc::classSummaryColorScale() {
    QCOMPARE(commonMissRate({0, 0.1, 0.3, 0.6, 0.9}), 0.3);  // past half is red, not "most common"
    QCOMPARE(commonMissRate({0, 0}), 0.5);                     // no one missed anything
    QCOMPARE(commonMissRate({0.7, 1.0}), 0.5);                 // every miss is by most of the class
    QCOMPARE(commonMissRate({0.5}), 0.5);                      // exactly half is not past half
    QCOMPARE(missLevel(0, 0.3), 0.0);
    QCOMPARE(missLevel(0.15, 0.3), 0.5);
    QCOMPARE(missLevel(0.3, 0.3), 1.0);  // the most commonly missed: yellow
    QCOMPARE(missLevel(0.45, 0.3), 1.0);
    QCOMPARE(missLevel(0.5, 0.5), 1.0);
    QCOMPARE(missLevel(0.51, 0.3), 2.0);  // missed by most: red
}

void TestMisc::formatting() {
    QCOMPARE(fmtLen(0.75), QStringLiteral("0.750"));
    QCOMPARE(fmtLen(0.7501, 0.75), QStringLiteral("0.7501"));
    QCOMPARE(fmtCoord(3), QStringLiteral("3.0"));
    QCOMPARE(fmtCoord(0.125), QStringLiteral("0.125"));
    QCOMPARE(fmtCoord(-0.0000001), QStringLiteral("0.0"));
    QCOMPARE(fmtPt({3, 2.5}), QStringLiteral("(3.0, 2.5)"));
    // Offsets and turns below the usual digits still show, for a tight tolerance.
    QCOMPARE(fmtOffset({0, -2.99e-5}), QStringLiteral("(0.0, -0.00003)"));
    QCOMPARE(fmtOffset({3, 3}), QStringLiteral("(3.0, 3.0)"));
    QCOMPARE(fmtOffset({1e-12, 0}), QStringLiteral("(0.0, 0.0)"));  // rounding noise stays 0
    QCOMPARE(fmtDeg(deg(1)), QStringLiteral("1.00\u00B0"));
    QCOMPARE(fmtDeg(deg(0.0004), 0), QStringLiteral("0.0004\u00B0"));
    QCOMPARE(fmtDeg(deg(45.001), deg(45)), QStringLiteral("45.001\u00B0"));
    // Lineweight codes (D100): 0 to 23 are AutoCAD's widths, 29 ByLayer, 30 ByBlock, 31 Default.
    QCOMPARE(lineweightFromCode(0), 0);
    QCOMPARE(lineweightFromCode(11), 50);
    QCOMPARE(lineweightFromCode(14), 70);
    QCOMPARE(lineweightFromCode(23), 211);
    QCOMPARE(lineweightFromCode(29), kLineweightByLayer);
    QCOMPARE(lineweightFromCode(30), kLineweightByBlock);
    QCOMPARE(lineweightFromCode(31), kLineweightDefault);
    QCOMPARE(plottedLineweight(kLineweightDefault), 25);
    QCOMPARE(lineweightName(211), QStringLiteral("2.11 mm"));
    // Ellipses (D98).
    const Primitive el = ellipse({4.5, 1.5}, {0, 1}, {-0.6, 0}, 0, kPi, false);
    QCOMPARE(describe(el), QStringLiteral("elliptical arc 2.000 \u00D7 1.200"));
    QCOMPARE(describeGeometry(el),
             QStringLiteral("center (4.5, 1.5), axes 2.000 \u00D7 1.200, major axis at 90.00\u00B0, 0.00\u00B0 -> 180.00\u00B0 from it"));
    const Primitive el2 = ellipse({0, 0}, {1, 0}, {0, 0.5});
    QCOMPARE(fmtAxes(el2, &el), QStringLiteral("2.000 \u00D7 1.000"));
    QCOMPARE(fmtAxes(ellipse({0, 0}, {1.00001, 0}, {0, 0.5}), &el2), QStringLiteral("2.00002 \u00D7 1.000"));
    // Colors: the ACI palette, names, differences, and dwgread's encodings.
    QCOMPARE(aciToRgb(1), 0xFF0000u);
    QCOMPARE(aciToRgb(21), 0xFF9F7Fu);
    QCOMPARE(aciToRgb(60), 0xBFFF00u);
    QCOMPARE(aciToRgb(240), 0xFF003Fu);
    QCOMPARE(aciToRgb(250), 0x333333u);
    QCOMPARE(colorName(1, 0xFF0000), QStringLiteral("red (ACI 1)"));
    QCOMPARE(colorName(7, 0xFFFFFF), QStringLiteral("black/white (ACI 7)"));
    QCOMPARE(colorDifference(7, 0xFFFFFF, -1, 0x000000), 0.0);  // ACI 7 shows black on white
    QCOMPARE(colorDifference(-1, 0xFFFFFF, 7, 0xFFFFFF), 0.0);
    QVERIFY(colorDifference(7, 0xFFFFFF, 1, 0xFF0000) > 50);
    QCOMPARE(colorName(34, aciToRgb(34)), QStringLiteral("ACI 34"));
    QCOMPARE(colorName(-1, 0xFA0505), QStringLiteral("RGB 250,5,5"));
    QCOMPARE(colorDifference(0xFF0000, 0xFF0000), 0.0);
    QVERIFY(std::fabs(colorDifference(0xFF0000, 0xFA0505) - 1.96) < 0.01);
    QVERIFY(std::fabs(colorDifference(0x000000, 0xFFFFFF) - 100.0) < 1e-9);
    QCOMPARE(parseDwgColor(QJsonObject{{"index", 256}, {"rgb", "000000"}, {"flag", 1}}).kind, DwgColor::Kind::ByLayer);
    QCOMPARE(parseDwgColor(QJsonObject{{"rgb", "000000"}}).kind, DwgColor::Kind::ByBlock);
    QCOMPARE(parseDwgColor(QJsonObject{{"index", 1}, {"rgb", "c2ff0000"}, {"flag", 128}}).rgb, 0xFF0000u);
    QCOMPARE(parseDwgColor(QJsonObject{{"index", 256}, {"rgb", "c3000007"}}).aci, 7);
    QCOMPARE(parseDwgColor(QJsonValue(-3)).aci, 3);  // a turned-off layer (older files)
    Primitive h = line({0, 0}, {1.25, 0}, LtClass::Hidden);
    QCOMPARE(describe(h), QStringLiteral("hidden line 1.250 long"));
    QCOMPARE(describe(circle({0, 0}, 0.375)), QStringLiteral("circle \u00D80.750"));
}

void TestMisc::settingsRoundTrip() {
    AppSettings s;
    s.compare.mode = Mode::Orthographic;
    s.compare.linTol = 0.001;
    s.compare.allowMirror = true;
    s.compare.extras = ExtrasPolicy::ReportOnly;
    s.compare.arrangementSeverity = Severity::Warn;
    s.compare.checkHatches = true;
    QVERIFY(s.compare.checkHatchPattern);  // on by default
    s.compare.checkHatchPattern = false;
    s.canon.layerIgnoreRegex = "^CONST";
    s.canon.hiddenPatterns = {"HID*"};
    s.duplicates.templateFiles = {"a.dwg"};
    s.loader.timeoutSec = 12;
    s.compare.detectLocation = false;  // wrong location: every time
    s.compare.matchColor = true;
    s.compare.colorTolerance = 7;
    s.compare.requireByLayer = true;
    s.compare.matchLineweight = true;
    s.compare.readHatches = true;
    s.compare.readStrictness = 2;
    s.compare.readOnPaper = true;
    s.compare.readAsErrors = true;
    s.compare.locationPenaltyErrors = 0;
    const AppSettings t = AppSettings::fromJson(s.toJson());
    // As text, as saved and loaded; text that is not settings is refused, not read as the
    // defaults (D76).
    const std::optional<AppSettings> fromText = AppSettings::fromJsonText(s.toJsonText(false));
    QVERIFY(fromText && fromText->compare.linTol == 0.001 && fromText->compare.mode == Mode::Orthographic);
    QString why;
    QVERIFY(!AppSettings::fromJsonText("{\"OBJECTS\": []}", &why));
    QVERIFY(why.contains(QStringLiteral("not a DWG Checker settings file")));
    QVERIFY(!AppSettings::fromJsonText("{\"compare\": {", &why));
    QVERIFY(why.contains(QStringLiteral("not valid JSON")));
    QVERIFY(!AppSettings::fromJsonText(QByteArray()));
    QVERIFY(!t.compare.detectLocation);
    QVERIFY(t.compare.matchColor && t.compare.requireByLayer && t.compare.matchLineweight);
    QVERIFY(!AppSettings{}.compare.matchLineweight);  // not counted by default
    QVERIFY(t.effectiveCanon().mergeByLineweight);
    QVERIFY(t.compare.readHatches && !t.compare.readDashes && t.compare.readOnPaper && t.compare.readAsErrors);
    QCOMPARE(t.compare.readStrictness, 2);
    QVERIFY(!AppSettings{}.compare.readHatches && AppSettings{}.compare.readStrictness == 1);  // off, medium by default
    // The levels (D101): medium is the proposal's numbers, loose and strict around it.
    const ReadLimits medium = readLimits(ReadStrictness::Medium), loose = readLimits(ReadStrictness::Loose),
                     strict = readLimits(ReadStrictness::Strict);
    QCOMPARE(medium.gapMm, 0.5);
    QCOMPARE(medium.hatchMm, 1.0);
    QCOMPARE(medium.textMm, 2.5);
    QVERIFY(loose.gapMm < medium.gapMm && medium.gapMm < strict.gapMm);
    QVERIFY(loose.textPx < medium.textPx && medium.textPx < strict.textPx);
    QVERIFY(loose.arrowMax > medium.arrowMax && medium.arrowMax > strict.arrowMax);
    QVERIFY2(readLimitsText(ReadStrictness::Strict).contains(QStringLiteral("text 3\u20135 mm or 15\u201360 px high")),
             qPrintable(readLimitsText(ReadStrictness::Strict)));
    // Ranges (D102): the upper ends shrink from loose to strict, and every range is a range.
    QVERIFY(loose.textMmMax > medium.textMmMax && medium.textMmMax > strict.textMmMax);
    QVERIFY(loose.hatchPxMax > medium.hatchPxMax && medium.hatchPxMax > strict.hatchPxMax);
    for (const ReadLimits& l : {loose, medium, strict})
        QVERIFY(l.gapMm < l.gapMmMax && l.gapPx < l.gapPxMax && l.hatchMm < l.hatchMmMax && l.hatchPx < l.hatchPxMax &&
                l.textMm < l.textMmMax && l.textPx < l.textPxMax);
    // AutoCAD's defaults printed 1:1 are within every level: 0.18" text, ANSI31 at scale 1, HIDDEN's gaps.
    for (const ReadLimits& l : {loose, medium, strict})
        QVERIFY(0.18 * 25.4 <= l.textMmMax && 0.125 * 25.4 <= l.hatchMmMax && 0.125 * 25.4 <= l.gapMmMax);
    QCOMPARE(t.compare.colorTolerance, 7.0);
    QCOMPARE(t.compare.locationPenaltyErrors, 0);
    QCOMPARE(t.compare.mode, Mode::Orthographic);
    QCOMPARE(t.compare.linTol, 0.001);
    QVERIFY(t.compare.allowMirror);
    QCOMPARE(t.compare.extras, ExtrasPolicy::ReportOnly);
    QCOMPARE(t.compare.arrangementSeverity, Severity::Warn);
    QVERIFY(t.compare.checkHatches);
    QVERIFY(!t.compare.checkHatchPattern);  // a choice saved with the version is kept
    // Settings saved before version 2 (no "version") take the new default.
    QJsonObject old = s.toJson();
    old.remove("version");
    QVERIFY(AppSettings::fromJson(old).compare.checkHatchPattern);
    QCOMPARE(t.canon.layerIgnoreRegex, QStringLiteral("^CONST"));
    QCOMPARE(t.canon.hiddenPatterns, QStringList{"HID*"});
    QCOMPARE(t.duplicates.templateFiles, QStringList{"a.dwg"});
    QCOMPARE(t.loader.timeoutSec, 12);
    // Defaults from the spec.
    const AppSettings d;
    QCOMPARE(d.compare.mode, Mode::Relative);
    QVERIFY(d.compare.detectLocation);  // wrong location counts once
    QCOMPARE(d.compare.locationPenaltyErrors, 1);
    QVERIFY(d.compare.matchLinetype);
    QVERIFY(d.compare.allowRotation);
    QVERIFY(!d.compare.allowMirror);
    QCOMPARE(d.compare.linTol, 1e-6);
    QCOMPARE(d.compare.angTol, deg(1e-6));  // 1e-6 degrees
    QVERIFY(!d.compare.requireLayerMatch);
    QCOMPARE(d.compare.extras, ExtrasPolicy::Penalize);
    QCOMPARE(d.compare.orthoTopK, 5);
    QCOMPARE(d.compare.orthoGapFraction, 0.02);
    QVERIFY(!d.compare.checkHatches);  // spec default: hatches are ignored
}

void TestMisc::csvEscaping() {
    QCOMPARE(csvEscape("plain"), QStringLiteral("plain"));
    QCOMPARE(csvEscape("a,b"), QStringLiteral("\"a,b\""));
    QCOMPARE(csvEscape("say \"hi\""), QStringLiteral("\"say \"\"hi\"\"\""));
    // Formula-looking text is neutralized; numbers are left alone (D76).
    QCOMPARE(csvEscape("=HYPERLINK(\"x\")"), QStringLiteral("\"'=HYPERLINK(\"\"x\"\")\""));
    QCOMPARE(csvEscape("@SUM(A1)"), QStringLiteral("'@SUM(A1)"));
    QCOMPARE(csvEscape("+1 extra"), QStringLiteral("'+1 extra"));
    QCOMPARE(csvEscape("-2.5"), QStringLiteral("-2.5"));
}

void TestMisc::displacedCopiesArePaired() {
    // A chain whose first line is drawn wrong: the rest of the chain has the right
    // lengths and directions but starts from the wrong point, far outside the
    // near-miss radius (1.885 here). An arc moved 9 down, and one unrelated extra circle.
    auto build = [](bool student) {
        DocBuilder b;
        // The student's first line ends at (6, 7) instead of (3, 4): another length and
        // direction, and not collinear (a collinear longer line would be an "extended" pair).
        const Vec2 o = student ? Vec2{3, 3} : Vec2{0, 0};
        b.line({0, 0}, student ? Vec2{6, 7} : Vec2{3, 4});
        b.line(Vec2{3, 4} + o, Vec2{10, 4} + o);            // 7 long
        b.line(Vec2{10, 4} + o, Vec2{12, 1} + o);           // 3.606 long
        b.circle({-2, 6}, 1);
        b.arc(student ? Vec2{14, -3} : Vec2{14, 6}, 0.5, 0, 90);
        if (student) b.circle({20, 20}, 0.3);
        return b.canon();
    };
    const Drawing ref = build(false), stu = build(true);
    CompareSettings s;
    s.mode = Mode::Absolute;
    const CompareResult r = compareDrawings(ref, stu, s, nullptr);
    const QString all = r.errorStrings().join(" | ");
    QCOMPARE(r.refCount, 5);
    QCOMPARE(r.matched, 1);
    QVERIFY2(r.errorStrings().contains(QStringLiteral("line 7.000 long at (6.5, 4.0) is shifted by (3.0, 3.0)")), qPrintable(all));
    QVERIFY2(r.errorStrings().contains(QStringLiteral("line 3.606 long at (11.0, 2.5) is shifted by (3.0, 3.0)")), qPrintable(all));
    QVERIFY2(r.errorStrings().contains(QStringLiteral("arc R0.500 center off by (0.0, -9.0)")), qPrintable(all));
    // The wrong first line has another shape: still missing + extra, like the unrelated circle.
    QVERIFY2(r.errorStrings().contains(QStringLiteral("missing line 5.000 long at (1.5, 2.0)")), qPrintable(all));
    QCOMPARE(r.penalizedExtras, 2);
    QVERIFY(qFuzzyCompare(r.correctness, 100.0 / 7));
    QCOMPARE(r.errorSummary(), QStringLiteral("3 shifted, 1 missing, 2 extra"));
}

void TestMisc::tinyShiftShowsItsSize() {
    // A student's line 0.00003 too low, with a tolerance of 0.000001: the message gives the
    // amount instead of "shifted by (0.0, -0.0)".
    auto build = [](double y) {
        DocBuilder b;
        b.line({0, 0}, {5, 0});
        b.line({0, y}, {5, y});
        b.circle({2.5, 3}, 0.5);
        return b.canon();
    };
    const Drawing ref = build(1), stu = build(1 - 3e-5);
    CompareSettings s;
    s.mode = Mode::Absolute;
    s.linTol = 1e-6;
    const CompareResult r = compareDrawings(ref, stu, s, nullptr);
    const QString all = r.errorStrings().join(" | ");
    QCOMPARE(r.matched, 2);
    QVERIFY2(r.errorStrings().contains(QStringLiteral("line 5.000 long at (2.5, 1.0) is shifted by (0.0, -0.00003)")),
             qPrintable(all));
    // At a looser tolerance (0.0001) the same drawing is exact.
    s.linTol = 1e-4;
    QVERIFY(compareDrawings(ref, stu, s, nullptr).pass);
}

void TestMisc::displacedLineTurnedSlightly() {
    // Right length, far from its place and turned 1 degree: one error that says both.
    // Turned 8 degrees (more than kDisplacedMaxTurn): missing + extra.
    auto build = [](bool student) {
        DocBuilder b;
        b.circle({5, 5}, 1);
        if (student) {
            b.line({20, 7}, Vec2{20, 7} + Vec2{std::cos(deg(1)), std::sin(deg(1))} * 10);
            b.line({20, 15}, Vec2{20, 15} + Vec2{std::cos(deg(8)), std::sin(deg(8))} * 6);
        } else {
            b.line({0, 0}, {10, 0});
            b.line({0, 10}, {6, 10});
        }
        return b.canon();
    };
    const Drawing ref = build(false), stu = build(true);
    CompareSettings s;
    s.mode = Mode::Absolute;
    const CompareResult r = compareDrawings(ref, stu, s, nullptr);
    const QString all = r.errorStrings().join(" | ");
    QCOMPARE(r.matched, 1);
    QVERIFY2(r.errorStrings().contains(QStringLiteral("line 10.000 long at (5.0, 0.0) is shifted by (19.999, 7.087) and rotated by 1.00°")),
             qPrintable(all));
    QVERIFY2(r.errorStrings().contains(QStringLiteral("missing line 6.000 long at (3.0, 10.0)")), qPrintable(all));
    QCOMPARE(r.penalizedExtras, 1);
    QVERIFY(qFuzzyCompare(r.correctness, 25.0));
    QCOMPARE(r.errorSummary(), QStringLiteral("1 shifted, 1 missing, 1 extra"));
}

void TestMisc::errorSummaryCounts() {
    // Labels are counted in the order the errors are listed; unlabeled items are warnings.
    CompareResult r;
    QCOMPARE(r.errorSummary(), QStringLiteral("no errors"));
    r.errors.push_back({ErrCat::Warning, QStringLiteral("warning: a"), -1, {}, {}, false, QString()});
    QCOMPARE(r.errorSummary(), QStringLiteral("no errors; 1 warning"));
    r.errors.push_back({ErrCat::NearMiss, QStringLiteral("line … is shifted"), -1, {}, {}, true, QStringLiteral("shifted")});
    r.errors.push_back({ErrCat::Missing, QStringLiteral("missing …"), -1, {}, {}, true, QStringLiteral("missing")});
    r.errors.push_back({ErrCat::NearMiss, QStringLiteral("line … is shifted"), -1, {}, {}, true, QStringLiteral("shifted")});
    r.errors.push_back({ErrCat::Warning, QStringLiteral("warning: b"), -1, {}, {}, false, QString()});
    QCOMPARE(r.errorSummary(), QStringLiteral("2 shifted, 1 missing; 2 warnings"));
    // Real comparisons.
    auto ref = loadFixture("ref");
    auto missing = loadFixture("ref_missing");
    auto mm = loadFixture("ref_mm");
    QVERIFY(ref && missing && mm);
    CompareSettings s;
    QVERIFY2(compareDrawings(*ref, *missing, s, nullptr).errorSummary().startsWith(QStringLiteral("1 missing")),
             qPrintable(compareDrawings(*ref, *missing, s, nullptr).errorSummary()));
    QVERIFY2(compareDrawings(*ref, *mm, s, nullptr).errorSummary().startsWith(QStringLiteral("1 wrong scale")),
             qPrintable(compareDrawings(*ref, *mm, s, nullptr).errorSummary()));
}

void TestMisc::dimensionsChecked() {
    DocBuilder r;
    r.line({0, 0}, {4, 0});
    r.dimension("LINEAR", 4.0, {0, 0}, {4, 0});
    DocBuilder s;
    s.line({0, 0}, {4, 0});
    s.dimension("LINEAR", 3.99, {0, 0}, {4, 0});
    const Drawing dr = r.canon(), ds = s.canon();
    CompareSettings cs;
    cs.mode = Mode::Absolute;
    QVERIFY(compareDrawings(dr, ds, cs, nullptr).pass);  // dimensions excluded by default
    cs.checkDimensions = true;
    const CompareResult res = compareDrawings(dr, ds, cs, nullptr);
    QVERIFY(!res.pass);
    QVERIFY(res.errorStrings().join("|").contains("dimension 4.000 shows 3.990"));
}

void TestMisc::similarityTransforms() {
    const Transform2D t = Transform2D::rotation(deg(90)) * Transform2D::scaling(3);
    QVERIFY(std::fabs(t.scaleFactor() - 3) < 1e-12);
    QVERIFY(std::fabs(t.rotationAngle() - deg(90)) < 1e-12);
    const Primitive c = transformed(circle({1, 0}, 0.5), t);
    QVERIFY(dist(c.center, {0, 3}) < 1e-12);
    QVERIFY(std::fabs(c.radius - 1.5) < 1e-12);
    const Primitive a = transformed(arc({0, 0}, 1, 0, kPi / 2), t);
    QVERIFY(std::fabs(a.radius - 3) < 1e-12);
    QVERIFY(angleDiff(a.a0, kPi / 2) < 1e-12 && angleDiff(a.a1, kPi) < 1e-12);
    Primitive dim;
    dim.kind = PrimKind::Dimension;
    dim.dimType = "LINEAR";
    dim.measurement = 4;
    QVERIFY(std::fabs(transformed(dim, t).measurement - 12) < 1e-12);
    dim.dimType = "ANG2LN";  // angles do not scale
    QVERIFY(std::fabs(transformed(dim, t).measurement - 4) < 1e-12);
    // Rigid transforms leave sizes exactly alone.
    QCOMPARE(transformed(circle({1, 0}, 0.375), Transform2D::rotation(deg(37))).radius, 0.375);
}

void TestMisc::scaleCandidateList() {
    Drawing r, s;
    r.meta.insUnits = 1;  // inches
    s.meta.insUnits = 4;  // millimeters
    const auto c = scaleCandidates(r, s);
    QVERIFY(!c.empty());
    QVERIFY(std::fabs(c.front() - 25.4) < 1e-9);  // the declared units come first
    for (double f : c) QVERIFY(std::fabs(f - 1) >= 1e-3);
    QCOMPARE(insUnitsMeters(1), 0.0254);
    QCOMPARE(insUnitsMeters(0), 0.0);
}

void TestMisc::unitlessFileHasNoUnitsWarning() {
    // INSUNITS 0 (unitless) says nothing about the units it was drawn in (D76).
    Drawing r, s;
    r.prims = {line({0, 0}, {3, 0}), line({0, 0}, {0, 2})};
    s.prims = r.prims;
    r.meta.insUnits = 1;
    s.meta.insUnits = 0;
    auto unitsWarning = [](const CompareResult& res) {
        for (const auto& e : res.errors)
            if (e.text.contains(QStringLiteral("units mismatch"))) return true;
        return false;
    };
    CompareSettings cs;
    QVERIFY(!unitsWarning(compareDrawings(r, s, cs, nullptr)));
    s.meta.insUnits = 4;
    QVERIFY(unitsWarning(compareDrawings(r, s, cs, nullptr)));
}

void TestMisc::singularInverseIsIdentity() {
    QVERIFY(Transform2D::scaling(0).inverse().isIdentity());
    const Transform2D t = Transform2D::rigid(deg(30), 2, 3);
    const Vec2 p{1.5, -0.5};
    QVERIFY(dist(t.inverse().apply(t.apply(p)), p) < 1e-12);
}

QTEST_GUILESS_MAIN(TestMisc)
#include "tst_misc.moc"
