// Optional hatch check on the hatch_* corpus (region A: 4 x 2 plate minus a D0.75
// hole, ANSI31; region B: 1 x 1 square at (5,0), ANSI37). Expected numbers were
// cross-checked with an independent Python port on the same fixtures.

#include "Comparator.h"
#include "HatchCheck.h"
#include "TestUtil.h"

#include <QtTest>

using namespace dc;
using namespace dct;

namespace {

CompareSettings hatchSettings(Mode m = Mode::Absolute, bool pattern = false) {
    CompareSettings s;
    s.mode = m;
    s.checkHatches = true;
    s.checkHatchPattern = pattern;
    return s;
}
bool anyError(const CompareResult& r, const QString& needle) {
    for (const auto& e : r.errors)
        if (e.text.contains(needle)) return true;
    return false;
}
QString dumpErrors(const CompareResult& r) { return r.errorStrings().join(" | "); }
const Primitive* firstHatch(const Drawing& d) {
    for (const auto& p : d.prims)
        if (p.kind == PrimKind::Hatch) return &p;
    return nullptr;
}

}  // namespace

class TestHatch : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void parsing();
    void polylinePathWithIslandSynthetic();
    void outerStyleStopsAtFirstIsland();
    void clockwiseEdgeArc();
    void offByDefault();
    void selfPasses();
    void missingRegion();
    void wrongPattern();
    void scaleAndAngleDoNotMatter();
    void partialRegion();
    void extraHatch();
    void spillOutside();
    void splitIntoTwoHatches();
    void solidFill();
    void relativeRotated();
    void insideRotatedBlock();
    void selectVersusPick();
    void scaledHatchedDrawing();

private:
    std::shared_ptr<Drawing> ref;
};

void TestHatch::initTestCase() {
    ref = loadFixture("hatch_ref");
    QVERIFY(ref);
}

void TestHatch::parsing() {
    QCOMPARE(countKind(*ref, PrimKind::Hatch), 2);
    int withIsland = 0;
    for (const auto& p : ref->prims)
        if (p.kind == PrimKind::Hatch) {
            QVERIFY(p.loops);
            if (p.text == "ANSI31") {
                QCOMPARE(p.loops->size(), size_t(2));  // outer rectangle + circular island
                ++withIsland;
                QVERIFY(hatchContains(p, {3, 1}));
                QVERIFY(!hatchContains(p, {1, 1}));     // inside the hole
                QVERIFY(!hatchContains(p, {5, 1}));
            } else {
                QCOMPARE(p.text, QStringLiteral("ANSI37"));
                QVERIFY(hatchContains(p, {5.5, 0.5}));
            }
        }
    QCOMPARE(withIsland, 1);
    // Hatches never take part in geometry matching or alignment.
    QVERIFY(!isGeometric(PrimKind::Hatch));
}

void TestHatch::polylinePathWithIslandSynthetic() {
    DocBuilder b;
    QJsonArray outer{QJsonObject{{"point", QJsonArray{0.0, 0.0}}}, QJsonObject{{"point", QJsonArray{4.0, 0.0}}},
                     QJsonObject{{"point", QJsonArray{4.0, 2.0}}}, QJsonObject{{"point", QJsonArray{0.0, 2.0}}}};
    QJsonArray hole{QJsonObject{{"point", QJsonArray{1.375, 1.0}}, {"bulge", -1.0}},
                    QJsonObject{{"point", QJsonArray{0.625, 1.0}}, {"bulge", -1.0}}};
    b.raw("HATCH", "0", {{"name", "_SOLID"}, {"is_solid_fill", 1}, {"style", 1},
                         {"paths", QJsonArray{QJsonObject{{"flag", 7}, {"closed", 1}, {"polyline_paths", outer}},
                                              QJsonObject{{"flag", 22}, {"closed", 1}, {"polyline_paths", hole}}}}});
    const Drawing d = b.canon();
    const Primitive* h = firstHatch(d);
    QVERIFY(h);
    QCOMPARE(h->text, QStringLiteral("SOLID"));  // "_SOLID" and the solid flag normalize to SOLID
    QVERIFY(hatchContains(*h, {0.1, 0.1}));
    QVERIFY(!hatchContains(*h, {1.0, 1.0}));
    QVERIFY(hatchContains(*h, {1.0, 1.5}));      // above the hole: the bulge arcs are flattened correctly
    // "Ignore islands" style fills the external loop only.
    DocBuilder i;
    i.raw("HATCH", "0", {{"name", "ANSI31"}, {"style", 2},
                         {"paths", QJsonArray{QJsonObject{{"flag", 7}, {"polyline_paths", outer}},
                                              QJsonObject{{"flag", 22}, {"polyline_paths", hole}}}}});
    const Drawing di = i.canon();
    QVERIFY(hatchContains(*firstHatch(di), {1.0, 1.0}));
}

void TestHatch::outerStyleStopsAtFirstIsland() {
    // A square plate, a square island, and a smaller square inside the island. "Normal" (style
    // 0) fills the innermost square again; "Outer" (style 1) stops at the first island (D76).
    auto square = [](double x0, double y0, double x1, double y1) {
        return QJsonArray{QJsonObject{{"point", QJsonArray{x0, y0}}}, QJsonObject{{"point", QJsonArray{x1, y0}}},
                          QJsonObject{{"point", QJsonArray{x1, y1}}}, QJsonObject{{"point", QJsonArray{x0, y1}}}};
    };
    auto hatch = [&](int style) {
        DocBuilder b;
        b.raw("HATCH", "0", {{"name", "ANSI31"}, {"style", style},
                             {"paths", QJsonArray{QJsonObject{{"flag", 1}, {"polyline_paths", square(0, 0, 10, 10)}},
                                                  QJsonObject{{"flag", 16}, {"polyline_paths", square(2, 2, 8, 8)}},
                                                  QJsonObject{{"flag", 16}, {"polyline_paths", square(4, 4, 6, 6)}}}}});
        return b.canon();
    };
    const Drawing normal = hatch(0), outer = hatch(1);
    QVERIFY(hatchContains(*firstHatch(normal), {1, 1}));
    QVERIFY(!hatchContains(*firstHatch(normal), {3, 3}));
    QVERIFY(hatchContains(*firstHatch(normal), {5, 5}));
    QVERIFY(hatchContains(*firstHatch(outer), {1, 1}));
    QVERIFY(!hatchContains(*firstHatch(outer), {3, 3}));
    QVERIFY(!hatchContains(*firstHatch(outer), {5, 5}));
}

void TestHatch::clockwiseEdgeArc() {
    // Rectangle with a semicircular notch; the notch is a clockwise edge arc stored 2pi -> 3pi.
    auto d = loadFixture("hatch_edges_cw");
    QVERIFY(d);
    const Primitive* h = firstHatch(*d);
    QVERIFY(h);
    QVERIFY(!hatchContains(*h, {2, 1.75}));  // inside the notch
    QVERIFY(hatchContains(*h, {2, 1.25}));   // below it
    QVERIFY(hatchContains(*h, {0.5, 1.9}));
}

void TestHatch::offByDefault() {
    auto d = loadFixture("hatch_missing_b");
    QVERIFY(d);
    CompareSettings s;
    s.mode = Mode::Absolute;
    const CompareResult r = compareDrawings(*ref, *d, s, nullptr);
    QVERIFY2(r.pass, qPrintable(dumpErrors(r)));  // the spec default: hatches are ignored
    QCOMPARE(r.refCount, 9);
}

void TestHatch::selfPasses() {
    const CompareResult r = compareDrawings(*ref, *ref, hatchSettings(Mode::Absolute, true), nullptr);
    QVERIFY2(r.pass, qPrintable(dumpErrors(r)));
    QCOMPARE(r.refCount, 11);  // 9 geometric primitives + 2 hatched regions
}

void TestHatch::missingRegion() {
    auto d = loadFixture("hatch_missing_b");
    QVERIFY(d);
    const CompareResult r = compareDrawings(*ref, *d, hatchSettings(), nullptr);
    QVERIFY(!r.pass);
    QCOMPARE(r.matched, 10);
    QVERIFY2(anyError(r, QStringLiteral("region at (5.5, 0.5) is not hatched")), qPrintable(dumpErrors(r)));
}

void TestHatch::wrongPattern() {
    auto d = loadFixture("hatch_pattern");
    QVERIFY(d);
    QVERIFY(compareDrawings(*ref, *d, hatchSettings(), nullptr).pass);  // region filled: enough by default
    const CompareResult r = compareDrawings(*ref, *d, hatchSettings(Mode::Absolute, true), nullptr);
    QVERIFY(!r.pass);
    QCOMPARE(r.matched, 10);
    QVERIFY2(anyError(r, QStringLiteral("is hatched with ANSI37, expected ANSI31")), qPrintable(dumpErrors(r)));
}

void TestHatch::scaleAndAngleDoNotMatter() {
    auto d = loadFixture("hatch_scale_angle");
    QVERIFY(d);
    const CompareResult r = compareDrawings(*ref, *d, hatchSettings(Mode::Absolute, true), nullptr);
    QVERIFY2(r.pass, qPrintable(dumpErrors(r)));
}

void TestHatch::partialRegion() {
    auto d = loadFixture("hatch_partial");
    QVERIFY(d);
    const CompareResult r = compareDrawings(*ref, *d, hatchSettings(), nullptr);
    QVERIFY(!r.pass);
    QCOMPARE(r.matched, 10);
    QCOMPARE(r.penalizedExtras, 0);  // the partial hatch lies inside the region: not an extra
    QVERIFY2(anyError(r, QStringLiteral("is only 5")) && anyError(r, QStringLiteral("% hatched")), qPrintable(dumpErrors(r)));
    QVERIFY2(r.errorSummary().startsWith(QStringLiteral("1 partly hatched")), qPrintable(r.errorSummary()));
}

void TestHatch::extraHatch() {
    auto d = loadFixture("hatch_extra");
    QVERIFY(d);
    const CompareResult r = compareDrawings(*ref, *d, hatchSettings(), nullptr);
    QVERIFY(!r.pass);
    QCOMPARE(r.matched, 11);
    QCOMPARE(r.penalizedExtras, 1);
    QVERIFY2(anyError(r, QStringLiteral("extra hatching (ANSI31) at (1.0, 1.0)")), qPrintable(dumpErrors(r)));
}

void TestHatch::spillOutside() {
    // The plate hatched by a drawn rectangle with no island: the hole is filled too (5.5% of the hatch).
    auto d = loadFixture("hatch_spill");
    QVERIFY(d);
    const CompareResult r = compareDrawings(*ref, *d, hatchSettings(), nullptr);
    QVERIFY(!r.pass);
    QCOMPARE(r.matched, 11);
    QCOMPARE(r.penalizedExtras, 1);
    QVERIFY2(anyError(r, QStringLiteral("also fills 6% outside the expected region")), qPrintable(dumpErrors(r)));
}

void TestHatch::splitIntoTwoHatches() {
    auto d = loadFixture("hatch_split");
    QVERIFY(d);
    QCOMPARE(countKind(*d, PrimKind::Hatch), 3);
    const CompareResult r = compareDrawings(*ref, *d, hatchSettings(Mode::Absolute, true), nullptr);
    QVERIFY2(r.pass, qPrintable(dumpErrors(r)));
}

void TestHatch::solidFill() {
    auto d = loadFixture("hatch_solid");
    QVERIFY(d);
    QVERIFY(compareDrawings(*ref, *d, hatchSettings(), nullptr).pass);
    const CompareResult r = compareDrawings(*ref, *d, hatchSettings(Mode::Absolute, true), nullptr);
    QVERIFY(!r.pass);
    QVERIFY2(anyError(r, QStringLiteral("is hatched with SOLID, expected ANSI31")), qPrintable(dumpErrors(r)));
}

void TestHatch::relativeRotated() {
    auto d = loadFixture("hatch_rotated");
    QVERIFY(d);
    const CompareResult r = compareDrawings(*ref, *d, hatchSettings(Mode::Relative, true), nullptr);
    QVERIFY2(r.pass, qPrintable(dumpErrors(r)));
}

void TestHatch::insideRotatedBlock() {
    auto d = loadFixture("hatch_block");
    QVERIFY(d);
    QCOMPARE(countKind(*d, PrimKind::Hatch), 2);
    const CompareResult r = compareDrawings(*ref, *d, hatchSettings(Mode::Relative, true), nullptr);
    QVERIFY2(r.pass, qPrintable(dumpErrors(r)));
}

void TestHatch::selectVersusPick() {
    // Same region: one hatch has polyline paths (picked point), the other edge paths (selected objects).
    auto pick = loadFixture("hatch_edges_pick");
    auto sel = loadFixture("hatch_edges_select");
    QVERIFY(pick && sel);
    QVERIFY2(compareDrawings(*pick, *sel, hatchSettings(Mode::Absolute, true), nullptr).pass, "select vs pick");
    QVERIFY2(compareDrawings(*sel, *pick, hatchSettings(Mode::Absolute, true), nullptr).pass, "pick vs select");
}

void TestHatch::scaledHatchedDrawing() {
    // Hatched regions are rescaled along with the geometry.
    const auto d = transformedCopy(*ref, Transform2D::scaling(2));
    const CompareResult r = compareDrawings(*ref, *d, hatchSettings(Mode::Absolute, true), nullptr);
    QVERIFY(r.scaled);
    QCOMPARE(r.matched, 11);
    QVERIFY(qFuzzyCompare(r.correctness, 100.0 * 11 / 12));
}

QTEST_GUILESS_MAIN(TestHatch)
#include "tst_hatch.moc"
