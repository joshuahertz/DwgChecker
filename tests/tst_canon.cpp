// Canonicalization: one or more tests per spec rule (1-10).

#include "TestUtil.h"

#include <QJsonDocument>
#include <QtTest>

Q_DECLARE_METATYPE(dc::Vec2)
Q_DECLARE_METATYPE(std::vector<dc::Vec2>)

using namespace dc;
using namespace dct;

class TestCanon : public QObject {
    Q_OBJECT
private slots:
    // Geometry helpers the rules depend on.
    void bulgeArcs();
    void ocsMinusZ();
    // Rule 1
    void modelSpaceOnly();
    void paperSpaceOption();
    void defpointsSkipped();
    // Rule 2
    void insertTranslateRotateScale();
    void nestedInsert();
    void mirroredInsertArc();
    void nonUniformArcIsElliptical();
    // Rule 3
    void lwpolylineSegments();
    void polylineEqualsLines();
    void polyline2d();
    // Rule 4
    void extrusionMinusZFlipsArc();
    // Rule 5
    void arcNormalization();
    void fullSweepArcBecomesCircle();
    // Rule 6
    void splitLineMerges();
    void overlappingLinesMerge();
    void collinearButSeparateStay();
    void differentLinetypesDoNotMerge();
    void zeroLengthDropped();
    void stackedDuplicateRemovedWithWarning();
    // Rule 7
    void contiguousArcsMerge();
    void twoHalvesMakeCircle();
    void arcOnCircleIsStacked();
    // Rule 8
    void annotationCategories();
    void hatchIgnoredSplineUnsupported();
    void ellipseEntity();
    void ellipseOfRatioOneIsACircle();
    void splineMatchesAutoCad_data();
    void splineMatchesAutoCad();
    void splineEntities();
    void splineInUnevenBlock();
    void splinesNotRead();
    void lineweightsResolved();
    // Rule 9
    void layerIgnoreRegex();
    // Rule 10
    void linetypeByLayer();
    void linetypeByBlock();
    void hatchArcStepsIgnoreLastDigit();
    void drawOrderFollowsSortentsTable();
    void linetypeExplicitAndPatterns();
    void layerZeroInheritance();
    void blockOnTurnedOffLayer();
    void nearColorsMergeWithinTolerance();
    // Misc
    void nonZeroZWarning();
    void nanCoordinatesLeftOut();
    void dimensionLooksAsDrawn();
};

void TestCanon::bulgeArcs() {
    BulgeArc a;
    // Semicircle CCW from (0,0) to (2,0): center (1,0), r 1, from 180 to 0 (i.e. through 270).
    QVERIFY(bulgeToArc({0, 0}, {2, 0}, 1.0, &a));
    QCOMPARE(a.center.x, 1.0);
    QVERIFY(std::fabs(a.center.y) < 1e-12);
    QCOMPARE(a.radius, 1.0);
    QVERIFY(std::fabs(a.startAngle - kPi) < 1e-12);
    QVERIFY(std::fabs(a.endAngle) < 1e-12);
    // Quarter arc used by the reference outline: (4,1.5) -> (3.5,2), center (3.5,1.5).
    QVERIFY(bulgeToArc({4, 1.5}, {3.5, 2}, std::tan(kPi / 8), &a));
    QVERIFY(dist(a.center, {3.5, 1.5}) < 1e-12);
    QVERIFY(std::fabs(a.radius - 0.5) < 1e-12);
    QVERIFY(std::fabs(a.startAngle) < 1e-12);
    QVERIFY(std::fabs(a.endAngle - kPi / 2) < 1e-12);
    // Negative bulge: clockwise, so the CCW arc runs from the end point to the start point.
    QVERIFY(bulgeToArc({0, 0}, {2, 0}, -1.0, &a));
    QVERIFY(std::fabs(a.startAngle) < 1e-12);
    QVERIFY(std::fabs(a.endAngle - kPi) < 1e-12);
    QVERIFY(!bulgeToArc({0, 0}, {2, 0}, 0.0, &a));
}

void TestCanon::ocsMinusZ() {
    const Affine3 m = Affine3::ocsToWcs({0, 0, -1});
    const Vec3 p = m.apply({1, 2, 3});
    QCOMPARE(p.x, -1.0);
    QCOMPARE(p.y, 2.0);
    QCOMPARE(p.z, -3.0);
    QVERIFY(Affine3::ocsToWcs({0, 0, 1}).isIdentity());
}

void TestCanon::modelSpaceOnly() {
    DocBuilder b;
    b.line({0, 0}, {1, 0});
    b.setPaperSpace(true);
    b.line({0, 0}, {5, 5});
    const Drawing d = b.canon();
    QCOMPARE(countKind(d, PrimKind::Line), 1);
    QVERIFY(findLine(d, {0, 0}, {1, 0}));
}

void TestCanon::paperSpaceOption() {
    DocBuilder b;
    b.line({0, 0}, {1, 0});
    b.setPaperSpace(true);
    b.line({0, 0}, {5, 5});
    CanonSettings s;
    s.usePaperSpace = true;
    const Drawing d = b.canon(s);
    QCOMPARE(countKind(d, PrimKind::Line), 1);
    QVERIFY(findLine(d, {0, 0}, {5, 5}));
}

void TestCanon::defpointsSkipped() {
    DocBuilder b;
    b.layer("Defpoints");
    b.line({0, 0}, {1, 0}, "Defpoints");
    b.line({0, 1}, {1, 1}, "0");
    QCOMPARE(countKind(b.canon(), PrimKind::Line), 1);
}

void TestCanon::insertTranslateRotateScale() {
    DocBuilder b;
    const quint64 blk = b.beginBlock("B", {1, 0, 0});
    b.line({1, 0}, {2, 0});       // relative to base (1,0): (0,0)-(1,0)
    b.circle({1, 1}, 0.5);        // relative (0,1)
    b.endBlock();
    b.insert(blk, {10, 20}, {2, 2, 2}, 90);
    const Drawing d = b.canon();
    QCOMPARE(d.prims.size(), size_t(2));
    // Base -> origin, scale 2, rotate 90, move to (10,20): (0,0)->(10,20), (1,0)->(10,22).
    QVERIFY(findLine(d, {10, 20}, {10, 22}, 1e-12));
    const Primitive* c = nullptr;
    for (const auto& p : d.prims) if (p.kind == PrimKind::Circle) c = &p;
    QVERIFY(c);
    QVERIFY(dist(c->center, {8, 20}) < 1e-12);  // (0,1) * 2 = (0,2), rotated 90 -> (-2,0)
    QVERIFY(std::fabs(c->radius - 1.0) < 1e-12);
}

void TestCanon::nestedInsert() {
    DocBuilder b;
    const quint64 inner = b.beginBlock("INNER");
    b.line({0, 0}, {1, 0});
    b.endBlock();
    const quint64 outer = b.beginBlock("OUTER");
    b.insert(inner, {5, 0});
    b.insert(inner, {0, 5}, {1, 1, 1}, 90);
    b.endBlock();
    b.insert(outer, {100, 100});
    const Drawing d = b.canon();
    QCOMPARE(countKind(d, PrimKind::Line), 2);
    QVERIFY(findLine(d, {105, 100}, {106, 100}, 1e-12));
    QVERIFY(findLine(d, {100, 105}, {100, 106}, 1e-12));
}

void TestCanon::mirroredInsertArc() {
    DocBuilder b;
    const quint64 blk = b.beginBlock("A");
    b.arc({0, 0}, 1, 0, 90);  // (1,0) -> (0,1)
    b.endBlock();
    b.insert(blk, {0, 0}, {-1, 1, 1});
    const Drawing d = b.canon();
    QCOMPARE(d.prims.size(), size_t(1));
    const Primitive& a = d.prims[0];
    QCOMPARE(a.kind, PrimKind::Arc);
    // Mirrored in x: (-1,0) -> (0,1), i.e. CCW from 90 to 180.
    QVERIFY(angleDiff(a.a0, kPi / 2) < 1e-12);
    QVERIFY(angleDiff(a.a1, kPi) < 1e-12);
}

void TestCanon::nonUniformArcIsElliptical() {
    // In a block scaled unevenly, an arc is part of an ellipse and a circle is an ellipse (D98).
    DocBuilder b;
    const quint64 blk = b.beginBlock("A");
    b.arc({0, 0}, 1, 0, 90);
    b.circle({0, 0}, 0.5);
    b.line({0, 0}, {1, 0});
    b.endBlock();
    b.insert(blk, {0, 0}, {2, 1, 1});
    b.insert(blk, {10, 0}, {-2, 1, 1});  // mirrored as well
    const Drawing d = b.canon();
    QCOMPARE(countKind(d, PrimKind::Unsupported), 0);
    QCOMPARE(countKind(d, PrimKind::Line), 2);
    QCOMPARE(countKind(d, PrimKind::Ellipse), 2);
    QCOMPARE(countKind(d, PrimKind::EllipseArc), 2);
    QVERIFY(findLine(d, {0, 0}, {2, 0}, 1e-12));  // lines scale fine
    for (const auto& p : d.prims) {
        if (p.kind == PrimKind::Ellipse) {
            QCOMPARE(p.radius, 1.0);
            QCOMPARE(p.minorRadius, 0.5);
            QCOMPARE(p.axisAngle, 0.0);
        }
        if (p.kind != PrimKind::EllipseArc) continue;
        QCOMPARE(p.radius, 2.0);
        QCOMPARE(p.minorRadius, 1.0);
        QVERIFY(std::fabs(p.axisAngle) < 1e-12);
        if (p.center.x < 5) {  // (2,0) round to (0,1), counter-clockwise
            QVERIFY(dist(p.arcStart(), {2, 0}) < 1e-12);
            QVERIFY(dist(p.arcEnd(), {0, 1}) < 1e-12);
        } else {  // mirrored: the same points, counter-clockwise from (10,1) to (8,0)
            QVERIFY(dist(p.arcStart(), {10, 1}) < 1e-12);
            QVERIFY(dist(p.arcEnd(), {8, 0}) < 1e-12);
            QVERIFY(dist(p.midpoint(), {10 - std::sqrt(2.0), std::sqrt(0.5)}) < 1e-12);
        }
    }
    for (const auto& w : d.warnings) QVERIFY2(w.kind != WarnKind::Unsupported, qPrintable(w.text));
}

void TestCanon::lwpolylineSegments() {
    DocBuilder b;
    // Open: (0,0)-(2,0) straight, (2,0)-(2,2) semicircle bulge 1, then straight.
    b.lwpolyline({{0, 0}, {2, 0}, {2, 2}, {0, 2}}, {0, 1, 0, 0}, false);
    const Drawing d = b.canon();
    QCOMPARE(countKind(d, PrimKind::Line), 2);
    QCOMPARE(countKind(d, PrimKind::Arc), 1);
    // Closed adds a closing segment.
    DocBuilder c;
    c.lwpolyline({{0, 0}, {2, 0}, {2, 2}}, {}, true);
    QCOMPARE(countKind(c.canon(), PrimKind::Line), 3);
}

void TestCanon::polylineEqualsLines() {
    DocBuilder a;
    a.lwpolyline({{1, 1}, {4, 1}, {4, 3}, {1, 3}}, {}, true);
    DocBuilder b;
    b.line({1, 1}, {4, 1}); b.line({4, 1}, {4, 3}); b.line({4, 3}, {1, 3}); b.line({1, 3}, {1, 1});
    const Drawing da = a.canon(), db = b.canon();
    QCOMPARE(da.prims.size(), size_t(4));
    QCOMPARE(db.prims.size(), size_t(4));
    for (const auto& p : db.prims) QVERIFY(findLine(da, p.p0, p.p1, 1e-12));
}

void TestCanon::polyline2d() {
    DocBuilder b;
    b.polyline2d({{0, 0}, {2, 0}, {2, 2}}, {0, 0, 0}, true);
    const Drawing d = b.canon();
    QCOMPARE(countKind(d, PrimKind::Line), 3);
    QVERIFY(findLine(d, {2, 2}, {0, 0}));
}

void TestCanon::extrusionMinusZFlipsArc() {
    // Values taken from tests/fixtures/ref_flipped3d.json (ROTATE3D 180 about X):
    // stored OCS center (-3.5,-1.5), 180 -> 270, extrusion (0,0,-1).
    DocBuilder b;
    b.arc({-3.5, -1.5}, 0.5, 180, 270, "0", QString(), {0, 0, -1});
    const Drawing d = b.canon();
    QCOMPARE(d.prims.size(), size_t(1));
    const Primitive& a = d.prims[0];
    QCOMPARE(a.kind, PrimKind::Arc);
    QVERIFY(dist(a.center, {3.5, -1.5}) < 1e-12);
    // In WCS this is the arc from 270 to 360 degrees.
    QVERIFY(angleDiff(a.a0, 3 * kPi / 2) < 1e-12);
    QVERIFY(angleDiff(a.a1, 0) < 1e-12);
    // The same flipped arc compares equal to an arc drawn directly in WCS.
    DocBuilder w;
    w.arc({3.5, -1.5}, 0.5, 270, 360);
    const Drawing wd = w.canon();  // keep the Drawing alive: prims[0] must not dangle
    QCOMPARE(wd.prims.size(), size_t(1));
    const Primitive& wa = wd.prims[0];
    QVERIFY(angleDiff(wa.a0, a.a0) < 1e-12 && angleDiff(wa.a1, a.a1) < 1e-12);
}

void TestCanon::arcNormalization() {
    DocBuilder b;
    b.arc({0, 0}, 1, -90, 450);  // stored angles outside [0, 360)
    const Drawing d = b.canon();
    QCOMPARE(d.prims.size(), size_t(1));
    QVERIFY(d.prims[0].a0 >= 0 && d.prims[0].a0 < kTwoPi);
    QVERIFY(d.prims[0].a1 >= 0 && d.prims[0].a1 < kTwoPi);
    QVERIFY(angleDiff(d.prims[0].a0, 3 * kPi / 2) < 1e-12);
    QVERIFY(angleDiff(d.prims[0].a1, kPi / 2) < 1e-12);
}

void TestCanon::fullSweepArcBecomesCircle() {
    DocBuilder b;
    b.arc({0, 0}, 1, 30, 30);
    QCOMPARE(b.canon().prims[0].kind, PrimKind::Circle);
}

void TestCanon::splitLineMerges() {
    DocBuilder b;
    b.line({0, 0}, {1.5, 0});
    b.line({1.5, 0}, {4, 0});
    const Drawing d = b.canon();
    QCOMPARE(d.prims.size(), size_t(1));
    QVERIFY(findLine(d, {0, 0}, {4, 0}, 0));  // exact endpoints preserved
    QCOMPARE(d.prims[0].mergedFrom.size(), 1);
}

void TestCanon::overlappingLinesMerge() {
    DocBuilder b;
    b.line({0, 0}, {3, 3});
    b.line({4, 4}, {2, 2});
    QCOMPARE(b.canon().prims.size(), size_t(1));
    QVERIFY(findLine(b.canon(), {0, 0}, {4, 4}, 0));
}

void TestCanon::collinearButSeparateStay() {
    DocBuilder b;
    b.line({0, 0}, {1, 0});
    b.line({2, 0}, {3, 0});
    QCOMPARE(b.canon().prims.size(), size_t(2));
}

void TestCanon::differentLinetypesDoNotMerge() {
    DocBuilder b;
    b.layer("HID", "HIDDEN");
    b.line({0, 0}, {1, 0});
    b.line({1, 0}, {2, 0}, "HID");
    QCOMPARE(b.canon().prims.size(), size_t(2));
}

void TestCanon::zeroLengthDropped() {
    DocBuilder b;
    b.line({1, 1}, {1, 1});
    b.line({0, 0}, {1, 0});
    QCOMPARE(b.canon().prims.size(), size_t(1));
}

void TestCanon::stackedDuplicateRemovedWithWarning() {
    DocBuilder b;
    b.line({0, 0}, {1, 0});
    b.line({1, 0}, {0, 0});  // same line, reversed
    b.circle({5, 5}, 1);
    b.circle({5, 5}, 1);
    const Drawing d = b.canon();
    QCOMPARE(d.prims.size(), size_t(2));
    QVERIFY(hasWarning(d, WarnKind::StackedDuplicate));
    bool found = false;
    for (const auto& w : d.warnings)
        if (w.kind == WarnKind::StackedDuplicate) { found = true; QVERIFY(w.text.contains("2")); }
    QVERIFY(found);
}

void TestCanon::contiguousArcsMerge() {
    DocBuilder b;
    b.arc({0, 0}, 1, 0, 45);
    b.arc({0, 0}, 1, 45, 120);
    const Drawing d = b.canon();
    QCOMPARE(d.prims.size(), size_t(1));
    QCOMPARE(d.prims[0].kind, PrimKind::Arc);
    QVERIFY(angleDiff(d.prims[0].a0, 0) < 1e-12);
    QVERIFY(angleDiff(d.prims[0].a1, deg(120)) < 1e-12);
    // Wrapping through 0 degrees.
    DocBuilder w;
    w.arc({0, 0}, 1, 300, 360);
    w.arc({0, 0}, 1, 0, 30);
    const Drawing dw = w.canon();
    QCOMPARE(dw.prims.size(), size_t(1));
    QVERIFY(angleDiff(dw.prims[0].a0, deg(300)) < 1e-12);
    QVERIFY(angleDiff(dw.prims[0].a1, deg(30)) < 1e-12);
}

void TestCanon::twoHalvesMakeCircle() {
    DocBuilder b;
    b.arc({2, 2}, 1, 0, 180);
    b.arc({2, 2}, 1, 180, 360);
    const Drawing d = b.canon();
    QCOMPARE(d.prims.size(), size_t(1));
    QCOMPARE(d.prims[0].kind, PrimKind::Circle);
}

void TestCanon::arcOnCircleIsStacked() {
    DocBuilder b;
    b.circle({0, 0}, 1);
    b.arc({0, 0}, 1, 10, 50);
    const Drawing d = b.canon();
    QCOMPARE(d.prims.size(), size_t(1));
    QVERIFY(hasWarning(d, WarnKind::StackedDuplicate));
}

void TestCanon::annotationCategories() {
    DocBuilder b;
    b.text({0, 0}, "PLATE");
    b.raw("MTEXT", "0", {{"ins_pt", pt(0, 1)}, {"text", "NOTE"}});
    b.dimension("LINEAR", 4.0, {0, 0}, {4, 0});
    b.line({0, 0}, {1, 0});
    const Drawing d = b.canon();
    QCOMPARE(countKind(d, PrimKind::Text), 2);
    QCOMPARE(countKind(d, PrimKind::Dimension), 1);
    QCOMPARE(countKind(d, PrimKind::Line), 1);
    for (const auto& p : d.prims)
        if (p.kind == PrimKind::Dimension) { QCOMPARE(p.measurement, 4.0); QCOMPARE(p.dimType, QStringLiteral("LINEAR")); }
}

void TestCanon::hatchIgnoredSplineUnsupported() {
    DocBuilder b;
    b.raw("HATCH", "0", {{"name", "ANSI31"}});
    b.raw("SPLINE", "0", {{"fit_pts", QJsonArray{pt(0, 0), pt(1, 1)}}, {"fit_tol", 0.1}});  // a fit tolerance (D99)
    b.raw("RAY", "0", {{"point", pt(3, 3)}, {"vector", pt(1, 0)}});
    const Drawing d = b.canon();
    QCOMPARE(countKind(d, PrimKind::Unsupported), 2);
    QCOMPARE(d.prims.size(), size_t(2));
    QVERIFY(hasWarning(d, WarnKind::Unsupported));
}

void TestCanon::ellipseEntity() {
    DocBuilder b;
    // Whole: center (1,2), its major semi-axis given from the other end (-2,0), half as wide.
    b.raw("ELLIPSE", "0", {{"center", pt(1, 2)}, {"sm_axis", pt(-2, 0)}, {"axis_ratio", 0.5}, {"start_angle", 0.0},
                           {"end_angle", 6.28318530717959}});  // 2 pi as dwgread prints it, a hair past
    // Part, with extrusion -Z: the minor axis is extrusion x major, here (1.5,0), so parameter 0
    // to pi/2 runs clockwise from (0,3) to (1.5,0). Read counter-clockwise, from (1.5,0).
    b.raw("ELLIPSE", "0", {{"center", pt(0, 0)}, {"sm_axis", pt(0, 3)}, {"axis_ratio", 0.5}, {"start_angle", 0.0},
                           {"end_angle", kPi / 2}, {"extrusion", pt(0, 0, -1)}});
    const Drawing d = b.canon();
    QCOMPARE(countKind(d, PrimKind::Unsupported), 0);
    QCOMPARE(d.prims.size(), size_t(2));
    const Primitive& whole = d.prims[0];
    QCOMPARE(whole.kind, PrimKind::Ellipse);
    QVERIFY(whole.center == Vec2(1, 2));
    QCOMPARE(whole.radius, 2.0);
    QCOMPARE(whole.minorRadius, 1.0);
    QCOMPARE(whole.axisAngle, 0.0);  // turned into [0, pi)
    const BBox box = whole.bbox();
    QVERIFY(std::fabs(box.x0 + 1) < 1e-12 && std::fabs(box.x1 - 3) < 1e-12);
    QVERIFY(std::fabs(box.y0 - 1) < 1e-12 && std::fabs(box.y1 - 3) < 1e-12);
    const Primitive& part = d.prims[1];
    QCOMPARE(part.kind, PrimKind::EllipseArc);
    QCOMPARE(part.radius, 3.0);
    QCOMPARE(part.minorRadius, 1.5);
    QVERIFY(std::fabs(part.axisAngle - kPi / 2) < 1e-12);
    QVERIFY(dist(part.arcStart(), {1.5, 0}) < 1e-12);
    QVERIFY(dist(part.arcEnd(), {0, 3}) < 1e-12);
    const BBox pb = part.bbox();
    QVERIFY(std::fabs(pb.x0) < 1e-12 && std::fabs(pb.x1 - 1.5) < 1e-12);
    QVERIFY(std::fabs(pb.y0) < 1e-12 && std::fabs(pb.y1 - 3) < 1e-12);
}

void TestCanon::ellipseOfRatioOneIsACircle() {
    DocBuilder b;
    b.raw("ELLIPSE", "0", {{"center", pt(1, 1)}, {"sm_axis", pt(0.6, 0.8)}, {"axis_ratio", 1.0}, {"start_angle", 0.0},
                           {"end_angle", 2 * kPi}});
    b.raw("ELLIPSE", "0", {{"center", pt(5, 1)}, {"sm_axis", pt(1, 0)}, {"axis_ratio", 1.0}, {"start_angle", 0.0},
                           {"end_angle", kPi / 2}});
    const Drawing d = b.canon();
    QCOMPARE(countKind(d, PrimKind::Circle), 1);
    QCOMPARE(countKind(d, PrimKind::Arc), 1);
    for (const auto& p : d.prims) {
        QVERIFY(std::fabs(p.radius - 1) < 1e-12);
        if (p.kind != PrimKind::Arc) continue;
        QVERIFY(p.center == Vec2(5, 1));
        QVERIFY(angleDiff(p.a0, 0) < 1e-12);
        QVERIFY(angleDiff(p.a1, kPi / 2) < 1e-12);
    }
}

namespace {
std::vector<Vec2> pts(std::initializer_list<double> xy) {
    std::vector<Vec2> v;
    for (auto it = xy.begin(); it != xy.end(); it += 2) v.push_back({*it, *(it + 1)});
    return v;
}
QJsonArray jsonPts(const std::vector<Vec2>& v) {
    QJsonArray a;
    for (Vec2 q : v) a.append(pt(q));
    return a;
}
}  // namespace

// AutoCAD's own control points for its fit-point splines (entget at 16 digits, in AutoCAD
// 2026's console and, for square-root and uniform knots, the instructor's full AutoCAD):
// the spline built through the same points must have them (D99).
void TestCanon::splineMatchesAutoCad_data() {
    QTest::addColumn<std::vector<Vec2>>("fit");
    QTest::addColumn<int>("knots");
    QTest::addColumn<Vec2>("t0");
    QTest::addColumn<Vec2>("t1");
    QTest::addColumn<std::vector<Vec2>>("ctrl");
    QTest::newRow("chord") << pts({0, 0, 1, 1, 2, 0.5, 3, 1.5, 4, 0}) << 0 << Vec2{} << Vec2{}
                           << pts({0, 0, 0.3023018502171830, 0.5423469190788796, 0.8435942973267072, 1.513456724723831,
                                   2.152354397288310, -0.1062262722668785, 3.114585686454278, 2.370934086034161,
                                   3.682016308467709, 0.8514865431051653, 4, 0});
    QTest::newRow("tangents") << pts({0, 9, 1, 10, 2, 9.5, 3, 10.5, 4, 9}) << 0 << Vec2{0, -1} << Vec2{1, 0}
                              << pts({0, 9, 0, 8.528595479208968, 0.9585177144574753, 10.89669180609605, 2.099437706909317,
                                      8.722831001170867, 3.231392509619489, 11.72720413395226, 3.399074787422668, 9, 4, 9});
    const Vec2 seam{0.2798068672827819, 0.9600563092972183};
    QTest::newRow("closed") << pts({0, 12, 1, 13, 2, 12.5, 3, 13.5, 0, 12}) << 0 << seam << seam
                            << pts({0, 12, 0.1319022221854796, 12.45257488441666, 0.9415768674993724, 13.54303827505654,
                                    2.022164593015334, 11.89180137744044, 4.336665057448514, 15.20365202227693,
                                    -0.3128335879077811, 10.92662441509193, 0, 12});
    QTest::newRow("two points") << pts({0, 21, 3, 22}) << 0 << Vec2{} << Vec2{}
                                << pts({0, 21, 1, 21.33333333333334, 2, 21.66666666666667, 3, 22});
    QTest::newRow("square root") << pts({0, 3, 1, 4, 2, 3.5, 3, 4.5, 4, 3}) << 1 << Vec2{} << Vec2{}
                                 << pts({0, 3, 0.3193412093886877, 3.529441370560916, 0.9226213674937913, 4.529630085167464,
                                         2.074349290992443, 2.848193916788939, 3.060030142391486, 5.327312259273284,
                                         3.674266261803851, 3.806498331863048, 4, 3});
    QTest::newRow("uniform") << pts({0, 6, 1, 7, 2, 6.5, 3, 7.5, 4, 6}) << 2 << Vec2{} << Vec2{}
                             << pts({0, 6, 0.3333333333333333, 6.517857142857142, 1, 7.553571428571427, 1.999999999999999,
                                     5.785714285714283, 3, 8.303571428571430, 3.666666666666666, 6.767857142857143, 4, 6});
}

void TestCanon::splineMatchesAutoCad() {
    QFETCH(std::vector<Vec2>, fit);
    QFETCH(int, knots);
    QFETCH(Vec2, t0);
    QFETCH(Vec2, t1);
    QFETCH(std::vector<Vec2>, ctrl);
    const auto s = splineThroughPoints(fit, knots, t0, t1);
    QVERIFY(s);
    QCOMPARE(s->ctrl.size(), ctrl.size());
    for (size_t i = 0; i < ctrl.size(); ++i) QVERIFY2(dist(s->ctrl[i], ctrl[i]) < 1e-12, qPrintable(QString::number(i)));
    // It goes through its points, and is the same curve read from the file (scenario 2).
    for (Vec2 q : fit) QVERIFY(distanceToSpline(*s, q) < 1e-12);
    QJsonObject o{{"scenario", 2}, {"degree", 3}, {"knotparam", knots}, {"fit_tol", 1e-10}, {"fit_pts", jsonPts(fit)},
                  {"beg_tan_vec", pt(t0)}, {"end_tan_vec", pt(t1)}};
    DocBuilder b;
    b.raw("SPLINE", "0", o);
    const Drawing d = b.canon();
    QCOMPARE(countKind(d, PrimKind::Spline), 1);
    const Primitive& p = d.prims[0];
    QVERIFY(dist(p.p0, fit.front()) < 1e-12 && dist(p.p1, fit.back()) < 1e-12);
    QVERIFY(splinesMatch(*p.spline, *s, 1e-12));
}

void TestCanon::splineEntities() {
    DocBuilder b;
    // A rational quadratic: a quarter of the unit circle.
    QJsonArray q{QJsonObject{{"x", 1.0}, {"y", 0.0}, {"z", 0.0}, {"w", 1.0}},
                 QJsonObject{{"x", 1.0}, {"y", 1.0}, {"z", 0.0}, {"w", std::sqrt(0.5)}},
                 QJsonObject{{"x", 0.0}, {"y", 1.0}, {"z", 0.0}, {"w", 1.0}}};
    b.raw("SPLINE", "0", {{"scenario", 1}, {"degree", 2}, {"rational", 1}, {"weighted", 1}, {"periodic", 0},
                          {"knots", QJsonArray{0.0, 0.0, 0.0, 1.0, 1.0, 1.0}}, {"ctrl_pts", q}});
    // A closed fit spline at a height, inside a block turned 90 degrees.
    const quint64 blk = b.beginBlock("S");
    b.raw("SPLINE", "0", {{"scenario", 2}, {"degree", 3}, {"knotparam", 0}, {"fit_tol", 1e-10},
                          {"fit_pts", QJsonArray{pt(0, 0, 2), pt(1, 1, 2), pt(2, 0, 2), pt(0, 0, 2)}},
                          {"beg_tan_vec", pt(0, 1)}, {"end_tan_vec", pt(0, 1)}});
    b.endBlock();
    b.insert(blk, {10, 0}, {1, 1, 1}, 90);
    const Drawing d = b.canon();
    QCOMPARE(countKind(d, PrimKind::Spline), 2);
    QCOMPARE(countKind(d, PrimKind::Unsupported), 0);
    const Primitive& arc = d.prims[0];
    QCOMPARE(arc.spline->from, SplineFrom::ControlPoints);
    QCOMPARE(arc.spline->weights.size(), size_t(3));
    for (Vec2 p : arc.spline->sp) QVERIFY(std::fabs(p.length() - 1) < 1e-12);
    QVERIFY(std::fabs(arc.size() - kPi / 2) < 1e-9);
    const Primitive& loop = d.prims[1];
    QCOMPARE(loop.spline->from, SplineFrom::FitPoints);
    QCOMPARE(loop.spline->fitCount(), size_t(3));
    QVERIFY(loop.spline->closed(1e-12));
    QVERIFY(dist(loop.p0, {10, 0}) < 1e-12);
    QVERIFY(distanceToSpline(*loop.spline, {9, 1}) < 1e-12);  // (1,1) turned 90 degrees about the insertion point
    QVERIFY(distanceToSpline(*loop.spline, {10, 2}) < 1e-12);
    QVERIFY(hasWarning(d, WarnKind::NonZeroZ));  // the height is ignored, as for other objects
}

void TestCanon::splineInUnevenBlock() {
    // In a block scaled (2, 1), a spline is its own curve stretched: built in the block, then
    // moved with its control points (rebuilding it through the stretched points would differ).
    const std::vector<Vec2> fit = pts({0, 0, 1, 1, 2, 0.5, 3, 1.5});
    DocBuilder b;
    const quint64 blk = b.beginBlock("S");
    b.raw("SPLINE", "0", {{"scenario", 2}, {"degree", 3}, {"knotparam", 0}, {"fit_tol", 1e-10}, {"fit_pts", jsonPts(fit)},
                          {"beg_tan_vec", pt(0, 0)}, {"end_tan_vec", pt(0, 0)}});
    b.endBlock();
    b.insert(blk, {0, 0}, {2, 1, 1});
    const Drawing d = b.canon();
    QCOMPARE(countKind(d, PrimKind::Spline), 1);
    const auto own = splineThroughPoints(fit, 0, {}, {});
    const SplineData& s = *d.prims[0].spline;
    QCOMPARE(s.ctrl.size(), own->ctrl.size());
    for (size_t i = 0; i < s.ctrl.size(); ++i) QVERIFY(dist(s.ctrl[i], {2 * own->ctrl[i].x, own->ctrl[i].y}) < 1e-12);
    std::vector<Vec2> stretched;
    for (Vec2 q : fit) stretched.push_back({2 * q.x, q.y});
    QVERIFY(!splinesMatch(s, *splineThroughPoints(stretched, 0, {}, {}), 1e-6));
}

void TestCanon::splinesNotRead() {
    DocBuilder b;
    const QJsonArray fit{pt(0, 0), pt(1, 1), pt(2, 0)};
    b.raw("SPLINE", "0", {{"scenario", 2}, {"degree", 3}, {"fit_tol", 0.01}, {"fit_pts", fit}});  // approximated
    b.raw("SPLINE", "0", {{"scenario", 2}, {"degree", 3}, {"knotparam", 15}, {"fit_pts", fit}});  // custom knots
    b.raw("SPLINE", "0", {{"scenario", 2}, {"degree", 3}, {"fit_pts", QJsonArray{pt(0, 0, 0), pt(1, 1, 1), pt(2, 0, 0)}}});
    b.raw("SPLINE", "0", {{"scenario", 1}, {"degree", 3}, {"knots", QJsonArray{0.0, 1.0}}, {"ctrl_pts", QJsonArray{}}});
    const Drawing d = b.canon();
    QCOMPARE(countKind(d, PrimKind::Unsupported), 4);
    QStringList why;
    for (const auto& p : d.prims) why << p.sourceType;
    QCOMPARE(why, (QStringList{QStringLiteral("SPLINE (fit tolerance)"), QStringLiteral("SPLINE (custom knots)"),
                               QStringLiteral("SPLINE (not flat)"), QStringLiteral("SPLINE (unreadable)")}));
}

// Lineweights (D100): the file's codes, ByBlock from the INSERT, Default at the top level.
void TestCanon::lineweightsResolved() {
    DocBuilder b;
    b.raw("LINE", "0", {{"start", pt(0, 0)}, {"end", pt(4, 0)}, {"linewt", 11}});  // 0.50 mm on the object
    b.raw("LINE", "0", {{"start", pt(0, 1)}, {"end", pt(4, 1)}, {"linewt", 30}});  // ByBlock at the top level
    b.line({0, 2}, {4, 2});                                                       // no code: by layer
    const quint64 blk = b.beginBlock("B");
    b.raw("LINE", "0", {{"start", pt(0, 0)}, {"end", pt(4, 0)}, {"linewt", 30}});
    b.endBlock();
    const quint64 i1 = b.insert(blk, {0, 3});
    Q_UNUSED(i1);
    b.raw("INSERT", "0", {{"ins_pt", pt(0, 4)}, {"scale", pt(1, 1, 1)}, {"rotation", 0.0}, {"block_header", href(blk)},
                          {"linewt", 14}});  // inserted at 0.70 mm
    const Drawing d = b.canon();
    struct Want { double y; int lw; bool byLayer; };
    for (const Want& w : {Want{0, 50, false}, Want{1, kLineweightDefault, false}, Want{2, kLineweightDefault, true},
                          Want{3, kLineweightDefault, true}, Want{4, 70, false}}) {
        const Primitive* p = findLine(d, {0, w.y}, {4, w.y});
        QVERIFY2(p, qPrintable(QString::number(w.y)));
        QCOMPARE(p->lineweight, w.lw);
        QCOMPARE(p->lwByLayer, w.byLayer);
    }
    // With lineweights graded, pieces of one edge merge only when they print the same width.
    DocBuilder m;
    m.raw("LINE", "0", {{"start", pt(0, 0)}, {"end", pt(2, 0)}, {"linewt", 11}});
    m.raw("LINE", "0", {{"start", pt(2, 0)}, {"end", pt(4, 0)}, {"linewt", 7}});
    QCOMPARE(countKind(m.canon(), PrimKind::Line), 1);
    CanonSettings s;
    s.mergeByLineweight = true;
    QCOMPARE(countKind(m.canon(s), PrimKind::Line), 2);
}

void TestCanon::layerIgnoreRegex() {
    DocBuilder b;
    b.layer("CONSTRUCTION");
    b.line({0, 0}, {1, 0}, "CONSTRUCTION");
    b.line({0, 1}, {1, 1});
    CanonSettings s;
    s.layerIgnoreRegex = "^CONST";
    QCOMPARE(b.canon(s).prims.size(), size_t(1));
    QCOMPARE(b.canon().prims.size(), size_t(2));
}

void TestCanon::linetypeByLayer() {
    DocBuilder b;
    b.layer("HID", "HIDDEN");
    b.line({0, 0}, {1, 0}, "HID");
    const Drawing d = b.canon();
    QCOMPARE(d.prims[0].ltClass, LtClass::Hidden);
    QCOMPARE(d.prims[0].ltypeName, QStringLiteral("HIDDEN"));
}

void TestCanon::linetypeByBlock() {
    // Like CMARK in ref_blocks: block lines with ltype ByBlock, insert with explicit CENTER.
    DocBuilder b;
    const quint64 blk = b.beginBlock("CM");
    b.line({-0.5, 0}, {0.5, 0}, "0", "ByBlock");
    b.endBlock();
    b.insert(blk, {1, 1}, {1, 1, 1}, 0, "0", "CENTER");
    b.line({0, 5}, {1, 5}, "0", "ByBlock");  // ByBlock in model space -> Continuous
    const Drawing d = b.canon();
    QCOMPARE(d.prims.size(), size_t(2));
    const Primitive* cl = findLine(d, {0.5, 1}, {1.5, 1});
    QVERIFY(cl);
    QCOMPARE(cl->ltClass, LtClass::Center);
    const Primitive* top = findLine(d, {0, 5}, {1, 5});
    QVERIFY(top);
    QCOMPARE(top->ltClass, LtClass::Visible);
}

void TestCanon::linetypeExplicitAndPatterns() {
    DocBuilder b;
    b.line({0, 0}, {1, 0}, "0", "HIDDEN2");
    b.line({0, 1}, {1, 1}, "0", "DASHEDX2");
    b.line({0, 2}, {1, 2}, "0", "CENTER2");
    b.line({0, 3}, {1, 3}, "0", "PHANTOM");
    b.line({0, 4}, {1, 4}, "0", "DOT");
    b.line({0, 5}, {1, 5}, "0", "Continuous");
    const Drawing d = b.canon();
    for (double y : {0.0, 1.0, 2.0, 3.0, 4.0, 5.0}) QVERIFY(findLine(d, {0, y}, {1, y}));
    QCOMPARE(findLine(d, {0, 0}, {1, 0})->ltClass, LtClass::Hidden);
    QCOMPARE(findLine(d, {0, 1}, {1, 1})->ltClass, LtClass::Hidden);
    QCOMPARE(findLine(d, {0, 2}, {1, 2})->ltClass, LtClass::Center);
    QCOMPARE(findLine(d, {0, 3}, {1, 3})->ltClass, LtClass::Phantom);
    QCOMPARE(findLine(d, {0, 4}, {1, 4})->ltClass, LtClass::Other);
    QCOMPARE(findLine(d, {0, 5}, {1, 5})->ltClass, LtClass::Visible);
    // Patterns are configurable.
    CanonSettings s;
    s.hiddenPatterns = {"DOT*"};
    const Drawing ds = b.canon(s);
    QVERIFY(findLine(ds, {0, 4}, {1, 4}));
    QCOMPARE(findLine(ds, {0, 4}, {1, 4})->ltClass, LtClass::Hidden);
}

void TestCanon::layerZeroInheritance() {
    // Like HIDLINE in ref_blocks: block line on layer 0, ByLayer, inserted on a HIDDEN layer.
    DocBuilder b;
    b.layer("HIDDEN", "HIDDEN");
    const quint64 blk = b.beginBlock("H");
    b.line({0, 0}, {0, 2}, "0");
    b.endBlock();
    b.insert(blk, {2, 0}, {1, 1, 1}, 0, "HIDDEN");
    const Drawing d = b.canon();
    QCOMPARE(d.prims.size(), size_t(1));
    QCOMPARE(d.prims[0].layer, QStringLiteral("HIDDEN"));
    QCOMPARE(d.prims[0].ltClass, LtClass::Hidden);
}

void TestCanon::blockOnTurnedOffLayer() {
    // A block on a turned-off layer: its layer-0 objects are hidden with it, those on a layer
    // that is on still show, as in AutoCAD. A frozen layer hides the whole block (D76).
    DocBuilder b;
    b.layer("OFF", "Continuous", 16 + 2);
    b.layer("FROZEN", "Continuous", 16 + 1);
    b.layer("PART");
    const quint64 blk = b.beginBlock("B");
    b.line({0, 0}, {1, 0}, "0");
    b.line({0, 1}, {1, 1}, "PART");
    b.endBlock();
    b.insert(blk, {0, 0}, {1, 1, 1}, 0, "OFF");
    b.insert(blk, {5, 0}, {1, 1, 1}, 0, "FROZEN");
    CanonSettings s;
    s.skipHiddenLayers = true;
    const Drawing d = b.canon(s);
    QCOMPARE(d.prims.size(), size_t(1));
    QVERIFY(findLine(d, {0, 1}, {1, 1}));
}

void TestCanon::nearColorsMergeWithinTolerance() {
    // An edge drawn in two pieces in near true colors: one line when colors must match within a
    // tolerance that covers them, two when they must be the same color (D76).
    DocBuilder b;
    b.entity("LINE", "0", {{"start", pt(0, 0)}, {"end", pt(1, 0)}, {"color", QJsonObject{{"rgb", "c2fa0505"}}}});
    b.entity("LINE", "0", {{"start", pt(1, 0)}, {"end", pt(2, 0)}, {"color", QJsonObject{{"rgb", "c2fb0606"}}}});
    CanonSettings s;
    s.mergeByColor = true;
    s.colorTolerance = 0;
    QCOMPARE(b.canon(s).prims.size(), size_t(2));
    s.colorTolerance = 5;
    const Drawing d = b.canon(s);
    QCOMPARE(d.prims.size(), size_t(1));
    QVERIFY(findLine(d, {0, 0}, {2, 0}));

    // Three pieces drifting 4 % at each step: the ends are 8 % apart, beyond the tolerance, so
    // the three do not all become one line, though each touching pair is within it.
    DocBuilder c;
    c.entity("LINE", "0", {{"start", pt(0, 0)}, {"end", pt(1, 0)}, {"color", QJsonObject{{"rgb", "c2fa0505"}}}});
    c.entity("LINE", "0", {{"start", pt(1, 0)}, {"end", pt(2, 0)}, {"color", QJsonObject{{"rgb", "c2e80505"}}}});
    c.entity("LINE", "0", {{"start", pt(2, 0)}, {"end", pt(3, 0)}, {"color", QJsonObject{{"rgb", "c2d60505"}}}});
    const Drawing chain = c.canon(s);
    QCOMPARE(chain.prims.size(), size_t(2));
    QVERIFY(!findLine(chain, {0, 0}, {3, 0}));
}

void TestCanon::nonZeroZWarning() {
    DocBuilder b;
    b.line3({0, 0, 0}, {1, 0, 0.5});
    const Drawing d = b.canon();
    QCOMPARE(d.prims.size(), size_t(1));
    QVERIFY(hasWarning(d, WarnKind::NonZeroZ));
}

void TestCanon::nanCoordinatesLeftOut() {
    // A parser build that prints nan: the object is left out with a warning, not drawn at 0 (D76).
    DocBuilder b;
    b.line({0, 0}, {1, 0});
    b.line({0, 1}, {12345.5, 1});
    QByteArray text = QJsonDocument(b.root()).toJson(QJsonDocument::Compact);
    QVERIFY(text.contains("12345.5"));
    text.replace("12345.5", "nan");
    DwgJson j;
    QString err;
    QVERIFY2(j.parse(text, &err), qPrintable(err));
    QVERIFY(j.hadNonFinite());
    const Drawing d = canonicalize(j, CanonSettings{});
    QCOMPARE(d.prims.size(), size_t(1));
    QVERIFY(findLine(d, {0, 0}, {1, 0}));
    QVERIFY(hasWarning(d, WarnKind::Load));
}

void TestCanon::hatchArcStepsIgnoreLastDigit() {
    // A hatch boundary with a quarter-circle edge, its end angle as a 14-decimal dump prints it
    // (1.5707963267949, just over pi/2) and at full precision: the same number of points.
    // (hatch_edges_select failed in CI on this: 33 points against 32.)
    auto loopSize = [](double endAngle) {
        DocBuilder b;
        auto edgeLine = [](Vec2 a, Vec2 e) {
            return QJsonObject{{"curve_type", 1}, {"first_endpoint", QJsonArray{a.x, a.y}}, {"second_endpoint", QJsonArray{e.x, e.y}}};
        };
        const QJsonObject arc{{"curve_type", 2}, {"center", QJsonArray{0.0, 0.0}}, {"radius", 1.0},
                              {"start_angle", 0.0}, {"end_angle", endAngle}, {"is_ccw", 1}};
        const QJsonArray segs{edgeLine({0, 0}, {1, 0}), arc, edgeLine({0, 1}, {0, 0})};
        b.raw(QStringLiteral("HATCH"), QStringLiteral("0"),
              QJsonObject{{"name", "SOLID"}, {"is_solid_fill", 1}, {"style", 0},
                          {"paths", QJsonArray{QJsonObject{{"flag", 1}, {"segs", segs}}}}});
        const Drawing d = b.canon();
        for (const auto& p : d.prims)
            if (p.kind == PrimKind::Hatch && p.loops && p.loops->size() == 1) return static_cast<int>(p.loops->front().size());
        return -1;
    };
    const int printed = loopSize(1.5707963267949), exact = loopSize(kPi / 2);
    QVERIFY(exact > 30);
    QCOMPARE(printed, exact);
}

void TestCanon::drawOrderFollowsSortentsTable() {
    // Three lines in file order a, m, c. DRAWORDER "send to back" on c writes a SORTENTSTABLE
    // that shifts the sort handles up by one place, as in a real file: a draws as m, m as c,
    // and c as a, so AutoCAD draws c, a, m. The primitives stay in file order (grading);
    // each carries its place in the draw order (display).
    auto build = [](bool table) {
        DocBuilder b;
        const quint64 ms = b.modelSpaceRecord();
        const quint64 a = b.line({0, 0}, {1, 0});
        const quint64 m = b.line({0, 1}, {1, 1});
        const quint64 c = b.line({0, 2}, {1, 2});
        if (table)
            b.object("SORTENTSTABLE", {{"block_owner", href(ms, 4)},
                                       {"ents", QJsonArray{href(a, 4), href(m, 4), href(c, 4)}},
                                       {"sort_ents", QJsonArray{href(m, 0), href(c, 0), href(a, 0)}}});
        return b.canon();
    };
    const Drawing sorted = build(true);
    QCOMPARE(sorted.prims.size(), size_t(3));
    for (int k = 0; k < 3; ++k) QCOMPARE(sorted.prims[k].p0.y, static_cast<double>(k));  // file order
    QCOMPARE(sorted.prims[0].drawOrder, 1);  // a
    QCOMPARE(sorted.prims[1].drawOrder, 2);  // m, on top
    QCOMPARE(sorted.prims[2].drawOrder, 0);  // c, sent to the back

    const Drawing plain = build(false);  // no table: the file order
    for (int k = 0; k < 3; ++k) QCOMPARE(plain.prims[k].drawOrder, k);
}

// A dimension keeps how AutoCAD draws it, from its *D block, for the as-drawn views (D87):
// extension lines and dimension lines as lines, filled arrowheads, an angular one's arc.
void TestCanon::dimensionLooksAsDrawn() {
    const auto d = loadFixture("dim_family");
    QVERIFY(d);
    const Primitive* dim = nullptr;
    for (const Primitive& p : d->prims)
        if (p.kind == PrimKind::Dimension && p.dimType == "LINEAR" && dist(p.p0, {0, 0}) < 1e-9 && dist(p.p1, {4, 0}) < 1e-9) dim = &p;
    QVERIFY(dim);
    QVERIFY(dim->dimLook);
    int lines = 0, fills = 0;
    for (const DimShape& s : dim->dimLook->shapes) {
        if (s.fill) {
            ++fills;
            QCOMPARE(s.pts.size(), size_t(3));  // a triangle: the SOLID's last two corners are one
        } else {
            ++lines;
            QCOMPARE(s.pts.size(), size_t(2));
        }
    }
    QCOMPARE(lines, 4);  // two extension lines, the dimension line in two parts around the text
    QCOMPARE(fills, 2);  // two arrowheads
    const DimShape& ext = dim->dimLook->shapes.front();  // the first extension line, below the 0,0 corner
    QVERIFY(dist(ext.pts[0], {0, -0.0625}) < 1e-9 && dist(ext.pts[1], {0, -0.78}) < 1e-9);
    // Every dimension in the file has its look; an angular one's includes its arc.
    const auto ang = loadFixture("dim_angprec");
    QVERIFY(ang);
    bool arc = false;
    for (const Primitive& p : ang->prims) {
        if (p.kind != PrimKind::Dimension) continue;
        QVERIFY(p.dimLook && !p.dimLook->shapes.empty());
        if (isAngularDimType(p.dimType))
            for (const DimShape& s : p.dimLook->shapes) arc = arc || (!s.fill && s.pts.size() > 2);
    }
    QVERIFY(arc);
    // Shown only as the file is drawn: a moved copy goes without (D88); unmoved, it keeps it.
    QVERIFY(!transformed(*dim, Transform2D::translation(10, 5)).dimLook);
    QVERIFY(transformed(*dim, Transform2D::identity()).dimLook);
}

QTEST_GUILESS_MAIN(TestCanon)
#include "tst_canon.moc"
