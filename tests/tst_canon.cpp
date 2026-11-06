// Canonicalization: one or more tests per spec rule (1-10).

#include "TestUtil.h"

#include <QtTest>

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
    void nonUniformArcUnsupported();
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
    // Rule 9
    void layerIgnoreRegex();
    // Rule 10
    void linetypeByLayer();
    void linetypeByBlock();
    void linetypeExplicitAndPatterns();
    void layerZeroInheritance();
    // Misc
    void nonZeroZWarning();
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

void TestCanon::nonUniformArcUnsupported() {
    DocBuilder b;
    const quint64 blk = b.beginBlock("A");
    b.arc({0, 0}, 1, 0, 90);
    b.line({0, 0}, {1, 0});
    b.endBlock();
    b.insert(blk, {0, 0}, {2, 1, 1});
    const Drawing d = b.canon();
    QCOMPARE(countKind(d, PrimKind::Unsupported), 1);
    QCOMPARE(countKind(d, PrimKind::Line), 1);
    QVERIFY(findLine(d, {0, 0}, {2, 0}, 1e-12));  // lines scale fine
    QVERIFY(hasWarning(d, WarnKind::NonUniformScale));
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
    b.raw("SPLINE", "0", {{"fit_pts", QJsonArray{pt(0, 0), pt(1, 1)}}});
    b.raw("ELLIPSE", "0", {{"center", pt(3, 3)}});
    const Drawing d = b.canon();
    QCOMPARE(countKind(d, PrimKind::Unsupported), 2);
    QCOMPARE(d.prims.size(), size_t(2));
    QVERIFY(hasWarning(d, WarnKind::Unsupported));
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

void TestCanon::nonZeroZWarning() {
    DocBuilder b;
    b.line3({0, 0, 0}, {1, 0, 0.5});
    const Drawing d = b.canon();
    QCOMPARE(d.prims.size(), size_t(1));
    QVERIFY(hasWarning(d, WarnKind::NonZeroZ));
}

QTEST_GUILESS_MAIN(TestCanon)
#include "tst_canon.moc"
