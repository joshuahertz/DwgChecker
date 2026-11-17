// End-to-end tests on the real corpus (tests/corpus/dwg, dumped to tests/fixtures
// with dwgread 0.14). Covers loading/metadata (M1), canonicalization on real
// files (M2), Absolute (M3) and Relative (M4) comparison.

#include "Comparator.h"
#include "Format.h"
#include "Readability.h"
#include "MetaExtractor.h"
#include "TestUtil.h"

#include <QFile>
#include <QJsonDocument>
#include <QtTest>

using namespace dc;
using namespace dct;

namespace {

CompareSettings relative() {
    CompareSettings s;
    s.mode = Mode::Relative;
    return s;
}
CompareSettings absolute() {
    CompareSettings s;
    s.mode = Mode::Absolute;
    return s;
}
bool anyError(const CompareResult& r, const QString& needle) {
    for (const auto& e : r.errors)
        if (e.text.contains(needle)) return true;
    return false;
}
QString dumpErrors(const CompareResult& r) { return r.errorStrings().join(" | "); }

}  // namespace

class TestCorpus : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    // M1: metadata from real files
    void refMetadata();
    void r2000Metadata();
    void saveAsKeepsFingerprint();
    // M2 on real files
    void refPrimitives();
    void flipped3dArc();
    void kitchenSink();
    void blocksEqualReference();
    void polylineEqualsReference();
    void splitEqualsReference();
    void rectanglePolylineEqualsLines();
    void r2000EqualsReference();
    // M3 Absolute
    void absoluteSelf();
    void absoluteTranslatedWrongLocation();
    // M4 Relative
    void relativeTranslated();
    void relativeRotated37();
    void relativeRedraw();
    void mirroredNeedsMirrorSetting();
    void flipped3dIsAMirror();
    void missingEntity();
    void extraConstructionLine();
    void sizeOffByHundredth();
    void unitsMismatch();
    void scaledRotatedAndMoved();
    void absoluteScaledAboutOrigin();
    void scaleOnlyWhenMostlyWrong();
    void copyPasteIsExact();
    void colorsAreRead();
    void colorsMustMatch();
    void colorTolerance();
    void colorAndLinetypeByLayer();
    // Ellipses (D98)
    void ellipsesRead();
    void ellipsesMatchInPlace_data();
    void ellipsesMatchInPlace();
    void ellipsesMatchMoved_data();
    void ellipsesMatchMoved();
    void ellipseNearMisses_data();
    void ellipseNearMisses();
    // Splines (D99)
    void splinesRead();
    void splinesFromFullAutoCad();
    void splineFitPolylineMatchesAutoCad();
    void splinesMatchInPlace_data();
    void splinesMatchInPlace();
    void splinesMatchMoved_data();
    void splinesMatchMoved();
    void splineNearMisses_data();
    void splineNearMisses();
    void splineWithFitToleranceIsNotGraded();
    // Lineweights (D100)
    void lineweightsRead();
    void lineweightsMustMatch();
    void lineweightSetByLayer();
    // Readability (D101)
    void readabilitySizesRead();
    void readabilityLevels_data();
    void readabilityLevels();
    void readabilityMessages();
    void readabilityInResults();
    void readabilityUpperLimits_data();
    void readabilityUpperLimits();

private:
    std::shared_ptr<Drawing> ref;
};

void TestCorpus::initTestCase() {
    ref = loadFixture("ref");
    QVERIFY2(ref, "tests/fixtures/ref.json must load");
}

void TestCorpus::refMetadata() {
    const DrawingMeta& m = ref->meta;
    QCOMPARE(m.dwgVersion, QStringLiteral("AC1032"));
    QCOMPARE(m.dwgVersionName, QStringLiteral("R2018"));
    QCOMPARE(m.fingerprintGuid, QStringLiteral("{51032796-B5F6-5940-B3CE-F2C26021DAA8}"));
    QCOMPARE(m.versionGuid, QStringLiteral("{FB2E7322-68D5-EF4C-AE84-2194A30359D4}"));
    QVERIFY(m.tducreate.valid);
    QCOMPARE(m.tducreate.day, 2461350);
    QCOMPARE(m.tducreate.ms, 49490000);
    QVERIFY(m.tdcreate.valid);  // from SummaryInfo (HEADER has no TDCREATE in dwgread 0.14)
    QCOMPARE(m.tdindwgSeconds, 1.0);
    QCOMPARE(m.handseed, quint64(669));
    QCOMPARE(m.insUnits, 1);
    QCOMPARE(m.lastSavedBy, QStringLiteral("jhertz"));
    QCOMPARE(m.appName, QStringLiteral("AutoCAD"));
    QCOMPARE(m.appRegistryVersion, QStringLiteral("25.1"));
    QVERIFY(m.hasSummaryInfo);
    QCOMPARE(m.tducreate.toDateTime().date(), QDate(2026, 11, 5));
}

void TestCorpus::r2000Metadata() {
    auto d = loadFixture("ref_r2000");
    QVERIFY(d);
    QCOMPARE(d->meta.dwgVersion, QStringLiteral("AC1015"));
    QVERIFY(!d->meta.hasSummaryInfo);
    QVERIFY(d->meta.lastSavedBy.isEmpty());
    QCOMPARE(d->meta.fingerprintGuid, ref->meta.fingerprintGuid);  // down-save keeps the fingerprint
    QVERIFY(d->meta.versionGuid != ref->meta.versionGuid);
}

void TestCorpus::saveAsKeepsFingerprint() {
    auto saveas = loadFixture("ref_saveas");
    auto copy = loadFixture("ref_copypaste");
    auto byte = loadFixture("ref_bytecopy");
    QVERIFY(saveas && copy && byte);
    QCOMPARE(saveas->meta.fingerprintGuid, ref->meta.fingerprintGuid);
    QVERIFY(saveas->meta.versionGuid != ref->meta.versionGuid);
    QVERIFY(copy->meta.fingerprintGuid != ref->meta.fingerprintGuid);
    QCOMPARE(byte->meta.versionGuid, ref->meta.versionGuid);
}

void TestCorpus::refPrimitives() {
    QCOMPARE(countKind(*ref, PrimKind::Line), 7);
    QCOMPARE(countKind(*ref, PrimKind::Arc), 1);
    QCOMPARE(countKind(*ref, PrimKind::Circle), 2);
    QCOMPARE(countKind(*ref, PrimKind::Text), 1);
    QCOMPARE(countKind(*ref, PrimKind::Dimension), 2);
    QCOMPARE(countKind(*ref, PrimKind::Unsupported), 0);
    const Primitive* hidden = findLine(*ref, {2, 0}, {2, 2});
    QVERIFY(hidden);
    QCOMPARE(hidden->ltClass, LtClass::Hidden);
    QCOMPARE(hidden->layer, QStringLiteral("HIDDEN"));
    const Primitive* center = findLine(*ref, {0.5, 1}, {1.5, 1});
    QVERIFY(center);
    QCOMPARE(center->ltClass, LtClass::Center);
    for (const auto& p : ref->prims)
        if (p.kind == PrimKind::Dimension && p.dimType == "LINEAR") QCOMPARE(p.measurement, 4.0);
}

void TestCorpus::flipped3dArc() {
    auto d = loadFixture("ref_flipped3d");
    QVERIFY(d);
    const Primitive* arc = nullptr;
    for (const auto& p : d->prims) if (p.kind == PrimKind::Arc) arc = &p;
    QVERIFY(arc);
    QVERIFY(dist(arc->center, {3.5, -1.5}) < 1e-9);
    QVERIFY(angleDiff(arc->a0, 3 * kPi / 2) < 1e-9);
    QVERIFY(angleDiff(arc->a1, 0) < 1e-9);
    int circlesBelow = 0;
    for (const auto& p : d->prims)
        if (p.kind == PrimKind::Circle && p.center.y < 0) ++circlesBelow;
    QCOMPARE(circlesBelow, 2);
}

void TestCorpus::kitchenSink() {
    auto d = loadFixture("kitchen");
    QVERIFY(d);
    // Defpoints and paper-space lines are skipped.
    QVERIFY(!findLine(*d, {0, 4}, {1, 4}));
    QVERIFY(!findLine(*d, {0, 0}, {2, 2}));
    // Explicit linetype and ByBlock in model space.
    const Primitive* explicitHidden = findLine(*d, {0, 0.5}, {1, 0.5});
    const Primitive* byBlock = findLine(*d, {0, 0.75}, {1, 0.75});
    QVERIFY(explicitHidden && byBlock);
    QCOMPARE(explicitHidden->ltClass, LtClass::Hidden);
    QCOMPARE(byBlock->ltClass, LtClass::Visible);
    // OCS -Z arc: OCS (-3,0) 30..120 -> WCS (3,0) 60..150.
    bool flippedArc = false;
    for (const auto& p : d->prims)
        if (p.kind == PrimKind::Arc && dist(p.center, {3, 0}) < 1e-9 && angleDiff(p.a0, deg(60)) < 1e-9 &&
            angleDiff(p.a1, deg(150)) < 1e-9)
            flippedArc = true;
    QVERIFY(flippedArc);
    // Closed LWPOLYLINE with extrusion -Z: closing segment (9,1)-(8,0) in WCS.
    QVERIFY(findLine(*d, {9, 1}, {8, 0}, 1e-9));
    // Old-style POLYLINE_2D closing segment.
    QVERIFY(findLine(*d, {12, 1}, {10, 0}, 1e-9));
    // Annotation categories.
    QCOMPARE(countKind(*d, PrimKind::Text), 2);
    QCOMPARE(countKind(*d, PrimKind::Dimension), 4);
    // Unsupported: XLINE, MINSERT (no counts in 0.14 JSON). The SPLINE is read (D99).
    QCOMPARE(countKind(*d, PrimKind::Unsupported), 2);
    QCOMPARE(countKind(*d, PrimKind::Spline), 1);
    for (const auto& p : d->prims)
        if (p.kind == PrimKind::Spline) {
            QCOMPARE(p.spline->fitCount(), size_t(4));
            QVERIFY(dist(p.p0, {0, 12}) < 1e-12 && dist(p.p1, {3, 13}) < 1e-12);
        }
    QVERIFY(hasWarning(*d, WarnKind::Unsupported));
    // Ellipses (D98): the ELLIPSE 2 x 1 at (6,12), and the circle r0.25 and arc r0.35 of the
    // block inserted at (6,6) at scale (2,1).
    QCOMPARE(countKind(*d, PrimKind::Ellipse), 2);
    QCOMPARE(countKind(*d, PrimKind::EllipseArc), 1);
    for (const auto& p : d->prims) {
        if (p.kind == PrimKind::Ellipse && dist(p.center, {6, 12}) < 1e-9) {
            QCOMPARE(p.radius, 1.0);
            QCOMPARE(p.minorRadius, 0.5);
            QCOMPARE(p.axisAngle, 0.0);
        } else if (p.kind == PrimKind::Ellipse) {
            QVERIFY(dist(p.center, {6, 6}) < 1e-9);
            QVERIFY(std::fabs(p.radius - 0.5) < 1e-12 && std::fabs(p.minorRadius - 0.25) < 1e-12);
        } else if (p.kind == PrimKind::EllipseArc) {
            QVERIFY(dist(p.arcStart(), {6.7, 6}) < 1e-9);
            QVERIFY(dist(p.arcEnd(), {6, 6.35}) < 1e-9);
        }
    }
    QVERIFY(hasWarning(*d, WarnKind::NonZeroZ));
    QVERIFY(hasWarning(*d, WarnKind::HiddenLayer));
    // Insert on layer HIDDEN: layer-0 circle inherits HIDDEN; the ByBlock line inherits its linetype.
    bool hiddenCircle = false;
    for (const auto& p : d->prims)
        if (p.kind == PrimKind::Circle && dist(p.center, {0, 6}) < 1e-9) {
            hiddenCircle = p.ltClass == LtClass::Hidden && std::fabs(p.radius - 0.5) < 1e-12;
        }
    QVERIFY(hiddenCircle);
}

void TestCorpus::blocksEqualReference() {
    auto d = loadFixture("ref_blocks");
    QVERIFY(d);
    const CompareResult r = compareDrawings(*ref, *d, absolute(), nullptr);
    QVERIFY2(r.pass, qPrintable(dumpErrors(r)));
    QCOMPARE(r.matched, 10);
}

void TestCorpus::polylineEqualsReference() {
    auto d = loadFixture("ref_polyline");
    QVERIFY(d);
    const CompareResult r = compareDrawings(*ref, *d, absolute(), nullptr);
    QVERIFY2(r.pass, qPrintable(dumpErrors(r)));
}

void TestCorpus::splitEqualsReference() {
    auto d = loadFixture("ref_split");
    QVERIFY(d);
    QCOMPARE(countKind(*d, PrimKind::Line), 7);  // the two halves were merged
    const CompareResult r = compareDrawings(*ref, *d, absolute(), nullptr);
    QVERIFY2(r.pass, qPrintable(dumpErrors(r)));
}

void TestCorpus::rectanglePolylineEqualsLines() {
    auto lines = loadFixture("rect_lines");
    auto pline = loadFixture("rect_pline");
    QVERIFY(lines && pline);
    const CompareResult r = compareDrawings(*lines, *pline, absolute(), nullptr);
    QVERIFY2(r.pass, qPrintable(dumpErrors(r)));
    QCOMPARE(r.matched, 4);
}

void TestCorpus::r2000EqualsReference() {
    auto d = loadFixture("ref_r2000");
    QVERIFY(d);
    const CompareResult r = compareDrawings(*ref, *d, absolute(), nullptr);
    QVERIFY2(r.pass, qPrintable(dumpErrors(r)));
}

void TestCorpus::absoluteSelf() {
    const CompareResult r = compareDrawings(*ref, *ref, absolute(), nullptr);
    QVERIFY(r.pass);
    QCOMPARE(r.correctness, 100.0);
    QVERIFY(r.errors.empty());
}

void TestCorpus::absoluteTranslatedWrongLocation() {
    // The reference moved by (5.25, 3.125), graded in Absolute mode.
    auto d = loadFixture("ref_translated");
    QVERIFY(d);
    // Once (the default): graded after moving it back, with one error for the location.
    CompareSettings s = absolute();
    QVERIFY(s.detectLocation && s.locationPenaltyErrors == 1);
    CompareResult r = compareDrawings(*ref, *d, s, nullptr);
    QVERIFY(r.moved);
    QCOMPARE(r.mode, Mode::Absolute);
    QCOMPARE(r.matched, 10);
    QVERIFY(!r.pass);
    QVERIFY(qFuzzyCompare(r.correctness, 100.0 * 10 / 11));
    QVERIFY(std::fabs(r.dx + 5.25) < 1e-9 && std::fabs(r.dy + 3.125) < 1e-9);  // student -> reference
    QCOMPARE(r.errors.front().cat, ErrCat::Location);
    QVERIFY(r.wrongLocation());  // the detail window draws the whole drawing yellow
    QVERIFY2(anyError(r, QStringLiteral("the whole drawing is shifted by (5.25, 3.125) from the reference; after moving it "
                                        "back, 10 of 10 reference items match")),
             qPrintable(dumpErrors(r)));
    QVERIFY2(r.errorSummary().startsWith(QStringLiteral("1 wrong location")), qPrintable(r.errorSummary()));
    QCOMPARE(r.fit(), 100.0);  // the file still fits this drawing perfectly (Batch)
    QCOMPARE(r.absoluteMatched, 0);  // copy detection sees the file as submitted

    // Never: moved back with no error, so it passes.
    s.locationPenaltyErrors = 0;
    r = compareDrawings(*ref, *d, s, nullptr);
    QVERIFY(r.moved);
    QVERIFY(!r.wrongLocation());  // not an error: matched items stay green
    QVERIFY2(r.pass, qPrintable(dumpErrors(r)));
    QCOMPARE(r.correctness, 100.0);
    QVERIFY2(anyError(r, QStringLiteral("graded after moving it back (wrong location is not counted as an error)")),
             qPrintable(dumpErrors(r)));

    // Every time: graded where it was drawn, with a warning that says it is only displaced.
    s.detectLocation = false;
    r = compareDrawings(*ref, *d, s, nullptr);
    QVERIFY(!r.moved);
    QVERIFY(!r.wrongLocation());
    QVERIFY(!r.pass);
    QCOMPARE(r.matched, 0);
    QCOMPARE(r.placementFreeCorrectness, 100.0);
    QVERIFY2(anyError(r, QStringLiteral("moved back, 10 of 10 reference items would match")), qPrintable(dumpErrors(r)));
}

void TestCorpus::relativeTranslated() {
    auto d = loadFixture("ref_translated");
    QVERIFY(d);
    const CompareResult r = compareDrawings(*ref, *d, relative(), nullptr);
    QVERIFY2(r.pass, qPrintable(dumpErrors(r)));
    QVERIFY(std::fabs(r.dx + 5.25) < 1e-9);
    QVERIFY(std::fabs(r.dy + 3.125) < 1e-9);
    QVERIFY(std::fabs(r.theta) < 1e-9);
    QCOMPARE(r.absoluteMatched, 0);
}

void TestCorpus::relativeRotated37() {
    auto d = loadFixture("ref_rotated37");
    QVERIFY(d);
    const CompareResult r = compareDrawings(*ref, *d, relative(), nullptr);
    QVERIFY2(r.pass, qPrintable(dumpErrors(r)));
    QVERIFY(angleDiff(r.theta, deg(-37)) < 1e-6);
    CompareSettings noRot = relative();
    noRot.allowRotation = false;
    QVERIFY(!compareDrawings(*ref, *d, noRot, nullptr).pass);
}

void TestCorpus::relativeRedraw() {
    auto d = loadFixture("ref_redraw");
    QVERIFY(d);
    const CompareResult r = compareDrawings(*ref, *d, relative(), nullptr);
    QVERIFY2(r.pass, qPrintable(dumpErrors(r)));
}

void TestCorpus::mirroredNeedsMirrorSetting() {
    auto d = loadFixture("ref_mirrored");
    QVERIFY(d);
    QCOMPARE(countKind(*d, PrimKind::Line), 7);
    const CompareResult r = compareDrawings(*ref, *d, relative(), nullptr);
    QVERIFY(!r.pass);
    CompareSettings m = relative();
    m.allowMirror = true;
    const CompareResult rm = compareDrawings(*ref, *d, m, nullptr);
    QVERIFY2(rm.pass, qPrintable(dumpErrors(rm)));
    QVERIFY(rm.mirrored);
}

void TestCorpus::flipped3dIsAMirror() {
    auto d = loadFixture("ref_flipped3d");
    QVERIFY(d);
    CompareSettings m = relative();
    m.allowMirror = true;
    const CompareResult r = compareDrawings(*ref, *d, m, nullptr);
    QVERIFY2(r.pass, qPrintable(dumpErrors(r)));
}

void TestCorpus::missingEntity() {
    auto d = loadFixture("ref_missing");
    QVERIFY(d);
    const CompareResult r = compareDrawings(*ref, *d, relative(), nullptr);
    QVERIFY(!r.pass);
    QCOMPARE(r.matched, 9);
    QCOMPARE(r.correctness, 90.0);
    QVERIFY2(anyError(r, QStringLiteral("missing circle Ø0.500 at (2.75, 1.0)")), qPrintable(dumpErrors(r)));
}

void TestCorpus::extraConstructionLine() {
    auto d = loadFixture("ref_extra");
    QVERIFY(d);
    const CompareResult r = compareDrawings(*ref, *d, relative(), nullptr);
    QVERIFY(!r.pass);
    QCOMPARE(r.matched, 10);
    QCOMPARE(r.penalizedExtras, 1);
    QVERIFY2(anyError(r, QStringLiteral("extra line 5.590 long at (2.0, 1.0) on layer CONSTRUCTION")), qPrintable(dumpErrors(r)));
    // Report-only extras pass; an ignore regex removes the construction layer entirely.
    CompareSettings rep = relative();
    rep.extras = ExtrasPolicy::ReportOnly;
    QVERIFY(compareDrawings(*ref, *d, rep, nullptr).pass);
    CanonSettings cs;
    cs.layerIgnoreRegex = "^CONSTRUCTION$";
    auto d2 = loadFixture("ref_extra", cs);
    QVERIFY(compareDrawings(*ref, *d2, relative(), nullptr).pass);
}

void TestCorpus::sizeOffByHundredth() {
    auto d = loadFixture("ref_sizeoff");
    QVERIFY(d);
    const CompareResult r = compareDrawings(*ref, *d, relative(), nullptr);
    QVERIFY(!r.pass);
    QCOMPARE(r.matched, 9);
    QCOMPARE(r.penalizedExtras, 0);  // paired as one near-miss, not missing + extra
    QVERIFY2(anyError(r, QStringLiteral("circle Ø0.750 drawn as Ø0.740")), qPrintable(dumpErrors(r)));
    QCOMPARE(r.errors.front().cat, ErrCat::NearMiss);
}

void TestCorpus::unitsMismatch() {
    // ref drawn in millimeters (x25.4, INSUNITS 4): one scale error, not every item wrong.
    auto d = loadFixture("ref_mm");
    QVERIFY(d);
    QCOMPARE(d->meta.insUnits, 4);
    const CompareResult r = compareDrawings(*ref, *d, relative(), nullptr);
    QVERIFY(!r.pass);
    QVERIFY(r.scaled);
    QVERIFY(std::fabs(r.scaleFactor - 25.4) < 1e-9);
    QCOMPARE(r.matched, 10);
    QVERIFY(qFuzzyCompare(r.correctness, 100.0 * 10 / 11));  // the scale error counts as one error
    QCOMPARE(r.errors.front().cat, ErrCat::Scale);
    QVERIFY2(r.errors.front().text.startsWith(QStringLiteral("drawing is at 25.4\u00D7 the reference scale (drawn in millimeters, "
                                                             "the reference is in inches)")),
             qPrintable(dumpErrors(r)));
    QVERIFY(!anyError(r, QStringLiteral("units mismatch")));  // folded into the scale error
    // A higher penalty lowers the value; switching detection off restores the old behaviour.
    CompareSettings heavy = relative();
    heavy.scalePenaltyErrors = 5;
    QVERIFY(qFuzzyCompare(compareDrawings(*ref, *d, heavy, nullptr).correctness, 100.0 * 10 / 15));
    CompareSettings off = relative();
    off.detectScale = false;
    const CompareResult r0 = compareDrawings(*ref, *d, off, nullptr);
    QVERIFY(!r0.scaled);
    QCOMPARE(r0.matched, 0);
    QVERIFY(anyError(r0, QStringLiteral("units mismatch")));
}

void TestCorpus::scaledRotatedAndMoved() {
    // Same units, but drawn twice as large, rotated 30 degrees and moved.
    const auto d = transformedCopy(*ref, Transform2D::translation(7, -3) * Transform2D::rotation(deg(30)) * Transform2D::scaling(2));
    const CompareResult r = compareDrawings(*ref, *d, relative(), nullptr);
    QVERIFY(r.scaled);
    QVERIFY(std::fabs(r.scaleFactor - 2) < 1e-9);
    QCOMPARE(r.matched, 10);
    QVERIFY(!r.pass);
    QVERIFY2(r.errors.front().text.startsWith(QStringLiteral("drawing is at 2\u00D7 the reference scale;")), qPrintable(dumpErrors(r)));
    // The overlay transform maps the submitted (unscaled) primitives onto the reference.
    for (size_t j = 0; j < d->prims.size(); ++j)
        if (r.stuStatus[j] == PrimStatus::Matched) {
            const Primitive back = transformed(d->prims[j], r.stuT[j]);
            QVERIFY(geometryMatches(ref->prims[r.stuPartner[j]], back, MatchCriteria{}));
        }
}

void TestCorpus::absoluteScaledAboutOrigin() {
    const auto d = transformedCopy(*ref, Transform2D::scaling(0.5));
    const CompareResult r = compareDrawings(*ref, *d, absolute(), nullptr);
    QVERIFY(r.scaled);
    QVERIFY(std::fabs(r.scaleFactor - 0.5) < 1e-12);
    QCOMPARE(r.matched, 10);
    QVERIFY2(anyError(r, QStringLiteral("drawing is at 1/2\u00D7 the reference scale")), qPrintable(dumpErrors(r)));
}

void TestCorpus::scaleOnlyWhenMostlyWrong() {
    // Ordinary mistakes never trigger the scale search.
    for (const char* name : {"ref_missing", "ref_sizeoff", "ref_extra", "ref_mirrored", "ref_translated"}) {
        auto d = loadFixture(name);
        QVERIFY(d);
        QVERIFY2(!compareDrawings(*ref, *d, relative(), nullptr).scaled, name);
    }
    auto t = loadFixture("ref_translated");
    QVERIFY(t);
    QVERIFY(!compareDrawings(*ref, *t, absolute(), nullptr).scaled);  // wrong placement is not a scale error
}

void TestCorpus::copyPasteIsExact() {
    auto d = loadFixture("ref_copypaste");
    QVERIFY(d);
    const CompareResult r = compareDrawings(*ref, *d, absolute(), nullptr);
    QVERIFY2(r.pass, qPrintable(dumpErrors(r)));
    QCOMPARE(r.absoluteMatched, 10);
}

// color_* (corpus.lsp dc-color-ref): horizontal lines 4 long at y = 0..7: by layer (white),
// ACI 1, true color 255,0,0, layer RED, layer TC (true color 200,10,10), an explicit HIDDEN
// linetype, and a ByBlock line in a block inserted in ACI 3 and by layer.
namespace {
const Primitive* lineAtY(const Drawing& d, double y) {
    for (const auto& p : d.prims)
        if (p.kind == PrimKind::Line && std::fabs(p.p0.y - y) < 1e-9 && std::fabs(p.p1.y - y) < 1e-9) return &p;
    return nullptr;
}
int countLabel(const CompareResult& r, const QString& label) {
    int n = 0;
    for (const auto& e : r.errors) n += e.label == label;
    return n;
}
}  // namespace

void TestCorpus::colorsAreRead() {
    auto d = loadFixture("color_ref");
    QVERIFY(d);
    struct Want { double y; int aci; quint32 rgb; bool colorByLayer, ltByLayer; };
    const Want want[] = {{0, 7, 0xFFFFFF, true, true},  {1, 1, 0xFF0000, false, true}, {2, -1, 0xFF0000, false, true},
                         {3, 1, 0xFF0000, true, true},  {4, -1, 0xC80A0A, true, true}, {5, 7, 0xFFFFFF, true, false},
                         {6, 3, 0x00FF00, false, true}, {7, 7, 0xFFFFFF, true, true}};
    for (const Want& w : want) {
        const Primitive* p = lineAtY(*d, w.y);
        QVERIFY2(p, qPrintable(QStringLiteral("no line at y = %1").arg(w.y)));
        QCOMPARE(p->aci, w.aci);
        QCOMPARE(p->rgb, w.rgb);
        QCOMPARE(p->colorByLayer, w.colorByLayer);
        QCOMPARE(p->ltByLayer, w.ltByLayer);
    }
}

void TestCorpus::colorsMustMatch() {
    auto r0 = loadFixture("color_ref"), wrong = loadFixture("color_wrong"), bylayer = loadFixture("color_bylayer");
    QVERIFY(r0 && wrong && bylayer);
    CompareSettings s = absolute();
    QVERIFY(compareDrawings(*r0, *wrong, s, nullptr).pass);  // colors are not checked by default
    s.matchColor = true;
    QVERIFY(compareDrawings(*r0, *r0, s, nullptr).pass);
    const CompareResult r = compareDrawings(*r0, *wrong, s, nullptr);
    QVERIFY(!r.pass);
    QCOMPARE(countLabel(r, QStringLiteral("wrong color")), 1);
    QVERIFY2(anyError(r, QStringLiteral("is blue (ACI 5), expected red (ACI 1)")), qPrintable(dumpErrors(r)));
    // The same colors set by layer instead (layer RED for ACI 1 and for 255,0,0) match.
    QVERIFY2(compareDrawings(*r0, *bylayer, s, nullptr).pass, qPrintable(dumpErrors(compareDrawings(*r0, *bylayer, s, nullptr))));
}

void TestCorpus::colorTolerance() {
    auto r0 = loadFixture("color_ref"), near = loadFixture("color_near");  // 250,5,5 for 255,0,0: 2.0 %
    QVERIFY(r0 && near);
    CompareSettings s = absolute();
    s.matchColor = true;
    const CompareResult exact = compareDrawings(*r0, *near, s, nullptr);
    QCOMPARE(countLabel(exact, QStringLiteral("wrong color")), 1);
    QVERIFY2(anyError(exact, QStringLiteral("is RGB 250,5,5, expected RGB 255,0,0")), qPrintable(dumpErrors(exact)));
    s.colorTolerance = 3;
    QVERIFY2(compareDrawings(*r0, *near, s, nullptr).pass, qPrintable(dumpErrors(compareDrawings(*r0, *near, s, nullptr))));
}

void TestCorpus::colorAndLinetypeByLayer() {
    auto r0 = loadFixture("color_ref"), bylayer = loadFixture("color_bylayer");
    QVERIFY(r0 && bylayer);
    CompareSettings s = absolute();
    s.matchColor = true;
    s.requireByLayer = true;
    QVERIFY2(compareDrawings(*r0, *bylayer, s, nullptr).pass, qPrintable(dumpErrors(compareDrawings(*r0, *bylayer, s, nullptr))));
    // Graded against the by-layer version, the original has four items set on the object:
    // two colors, the HIDDEN linetype, and the block inserted in green.
    const CompareResult r = compareDrawings(*bylayer, *r0, s, nullptr);
    QCOMPARE(countLabel(r, QStringLiteral("not by layer")), 4);
    QVERIFY2(anyError(r, QStringLiteral("has its color set on the object, not by layer")), qPrintable(dumpErrors(r)));
    QVERIFY2(anyError(r, QStringLiteral("has its linetype set on the object, not by layer")), qPrintable(dumpErrors(r)));
}

// The ellipse set (tests/corpus, D98): a 6 x 3 plate, a whole ellipse 2 x 1 at (1.5,1.5) with
// its major axis along (0.8,0.6), and the left half of an ellipse 2 x 1.2 at (4.5,1.5).
void TestCorpus::ellipsesRead() {
    auto d = loadFixture("ell_ref");
    QVERIFY(d);
    QCOMPARE(countKind(*d, PrimKind::Line), 4);
    QCOMPARE(countKind(*d, PrimKind::Ellipse), 1);
    QCOMPARE(countKind(*d, PrimKind::EllipseArc), 1);
    QCOMPARE(countKind(*d, PrimKind::Unsupported), 0);
    for (const auto& p : d->prims) {
        if (p.kind == PrimKind::Ellipse) {
            QVERIFY(dist(p.center, {1.5, 1.5}) < 1e-12);
            QVERIFY(std::fabs(p.radius - 1) < 1e-12 && std::fabs(p.minorRadius - 0.5) < 1e-12);
            QVERIFY(std::fabs(p.axisAngle - std::atan2(0.6, 0.8)) < 1e-12);
        } else if (p.kind == PrimKind::EllipseArc) {
            QVERIFY(dist(p.center, {4.5, 1.5}) < 1e-12);
            QVERIFY(std::fabs(p.radius - 1) < 1e-12 && std::fabs(p.minorRadius - 0.6) < 1e-12);
            QVERIFY(std::fabs(p.axisAngle - kPi / 2) < 1e-12);
            QVERIFY(dist(p.arcStart(), {4.5, 2.5}) < 1e-12);
            QVERIFY(dist(p.arcEnd(), {4.5, 0.5}) < 1e-12);
            QVERIFY(dist(p.midpoint(), {3.9, 1.5}) < 1e-12);  // the left half
        }
    }
}

// Where they were drawn: the same file, drawn again another way (lines reversed, each ellipse
// from the other end of its axis), and as a circle and an arc in unevenly scaled blocks.
void TestCorpus::ellipsesMatchInPlace_data() {
    QTest::addColumn<QString>("name");
    for (const char* n : {"ell_ref", "ell_redraw", "ell_block"}) QTest::newRow(n) << QString::fromLatin1(n);
}

void TestCorpus::ellipsesMatchInPlace() {
    QFETCH(QString, name);
    auto r0 = loadFixture("ell_ref");
    auto d = loadFixture(name);
    QVERIFY(r0 && d);
    const CompareResult r = compareDrawings(*r0, *d, absolute(), nullptr);
    QVERIFY2(r.pass, qPrintable(dumpErrors(r)));
    QCOMPARE(r.matched, 6);
}

// Moved, turned 37 degrees, mirrored (MIRROR gives the ellipses extrusion -Z) and flipped
// (ROTATE3D): found by Relative mode, a mirror only when mirrors are allowed.
void TestCorpus::ellipsesMatchMoved_data() {
    QTest::addColumn<QString>("name");
    QTest::addColumn<bool>("mirrored");
    QTest::newRow("translated") << QStringLiteral("ell_translated") << false;
    QTest::newRow("rotated37") << QStringLiteral("ell_rotated37") << false;
    QTest::newRow("mirrored") << QStringLiteral("ell_mirrored") << true;
    QTest::newRow("flipped3d") << QStringLiteral("ell_flipped3d") << true;
}

void TestCorpus::ellipsesMatchMoved() {
    QFETCH(QString, name);
    QFETCH(bool, mirrored);
    auto r0 = loadFixture("ell_ref");
    auto d = loadFixture(name);
    QVERIFY(r0 && d);
    CompareSettings s = relative();
    s.allowMirror = false;
    CompareResult r = compareDrawings(*r0, *d, s, nullptr);
    QCOMPARE(r.pass, !mirrored);
    if (mirrored) {
        s.allowMirror = true;
        r = compareDrawings(*r0, *d, s, nullptr);
        QVERIFY2(r.pass, qPrintable(dumpErrors(r)));
        QVERIFY(r.mirrored);
    }
    QVERIFY2(r.pass, qPrintable(dumpErrors(r)));
    QCOMPARE(r.matched, 6);
}

void TestCorpus::ellipseNearMisses_data() {
    QTest::addColumn<QString>("name");
    QTest::addColumn<QString>("label");
    QTest::addColumn<QString>("text");
    QTest::newRow("sizeoff") << QStringLiteral("ell_sizeoff") << QStringLiteral("wrong size")
                             << QStringLiteral("ellipse 2.000 \u00D7 1.000 at (1.5, 1.5): minor axis drawn 0.900");
    QTest::newRow("shifted") << QStringLiteral("ell_shifted") << QStringLiteral("shifted")
                             << QStringLiteral("ellipse 2.000 \u00D7 1.000 center off by (0.1, 0.0)");
    QTest::newRow("turned") << QStringLiteral("ell_turned") << QStringLiteral("rotated")
                            << QStringLiteral("ellipse 2.000 \u00D7 1.000 at (1.5, 1.5) is rotated by 10.00\u00B0");
    QTest::newRow("arcangle") << QStringLiteral("ell_arcangle") << QStringLiteral("wrong arc angle")
                              << QStringLiteral("elliptical arc 2.000 \u00D7 1.200 at (4.5, 1.5) runs from (4.5, 2.5) to");
    QTest::newRow("circle") << QStringLiteral("ell_circle") << QStringLiteral("wrong shape")
                            << QStringLiteral("ellipse 2.000 \u00D7 1.000 at (1.5, 1.5) drawn as a circle \u00D81.500");
}

void TestCorpus::ellipseNearMisses() {
    QFETCH(QString, name);
    QFETCH(QString, label);
    QFETCH(QString, text);
    auto r0 = loadFixture("ell_ref");
    auto d = loadFixture(name);
    QVERIFY(r0 && d);
    const CompareResult r = compareDrawings(*r0, *d, absolute(), nullptr);
    QVERIFY(!r.pass);
    QCOMPARE(r.refCount, 6);
    QCOMPARE(r.matched, 5);
    QCOMPARE(r.penalizedExtras, 0);  // the student's one is paired with the solution's
    int near = 0;
    for (const auto& e : r.errors) {
        if (e.cat != ErrCat::NearMiss) continue;
        ++near;
        QCOMPARE(e.label, label);
        QVERIFY2(e.text.startsWith(text), qPrintable(e.text));
    }
    QCOMPARE(near, 1);
}

// The spline set (tests/corpus, D99): a 6 x 3 plate whose top edge is a spline through 5
// points from (6,3) to (0,3), and a closed spline through 4 points inside it.
void TestCorpus::splinesRead() {
    auto d = loadFixture("spl_ref");
    QVERIFY(d);
    QCOMPARE(countKind(*d, PrimKind::Line), 3);
    QCOMPARE(countKind(*d, PrimKind::Spline), 2);
    QCOMPARE(countKind(*d, PrimKind::Unsupported), 0);
    for (const auto& p : d->prims) {
        if (p.kind != PrimKind::Spline) continue;
        QCOMPARE(p.spline->from, SplineFrom::FitPoints);
        if (p.spline->closed(1e-9)) {
            QCOMPARE(p.spline->fitCount(), size_t(4));
            QCOMPARE(describe(p), QStringLiteral("spline through 4 points"));
        } else {
            QVERIFY(dist(p.p0, {6, 3}) < 1e-12 && dist(p.p1, {0, 3}) < 1e-12);
            QCOMPARE(describe(p), QStringLiteral("spline through 5 points"));
            for (Vec2 q : p.spline->fit) QVERIFY(distanceToSpline(*p.spline, q) < 1e-12);
        }
    }
}

// Made by the instructor in full AutoCAD 2026 (the console's SPLINE has no Knots or Method
// options): the same 5 points with chord, square-root and uniform knots, and by control
// vertices (CV), each 3 higher. AutoCAD's own control points, read back at 16 digits.
void TestCorpus::splinesFromFullAutoCad() {
    auto d = loadFixture("spl_fullacad");
    QVERIFY(d);
    QCOMPARE(countKind(*d, PrimKind::Spline), 4);
    QCOMPARE(countKind(*d, PrimKind::Unsupported), 0);
    const std::vector<std::vector<double>> acad{
        {0, 0, 0.3023018502171830, 0.5423469190788796, 0.8435942973267072, 1.513456724723831, 2.152354397288310,
         -0.1062262722668785, 3.114585686454278, 2.370934086034161, 3.682016308467709, 0.8514865431051653, 4, 0},
        {0, 3, 0.3193412093886877, 3.529441370560916, 0.9226213674937913, 4.529630085167464, 2.074349290992443,
         2.848193916788939, 3.060030142391486, 5.327312259273284, 3.674266261803851, 3.806498331863048, 4, 3},
        {0, 6, 0.3333333333333333, 6.517857142857142, 1, 7.553571428571427, 1.999999999999999, 5.785714285714283, 3,
         8.303571428571430, 3.666666666666666, 6.767857142857143, 4, 6},
        {0, 9, 1, 10, 2, 9.5, 3, 10.5, 4, 9}};  // CV: the points clicked are its control points
    int seen = 0;
    for (const auto& p : d->prims) {
        if (p.kind != PrimKind::Spline) continue;
        const int row = static_cast<int>(std::lround(p.p0.y / 3));
        QVERIFY(row >= 0 && row < 4);
        QCOMPARE(p.spline->from, row == 3 ? SplineFrom::ControlPoints : SplineFrom::FitPoints);
        const std::vector<double>& c = acad[row];
        QCOMPARE(p.spline->ctrl.size() * 2, c.size());
        for (size_t i = 0; i < p.spline->ctrl.size(); ++i)
            QVERIFY2(dist(p.spline->ctrl[i], {c[2 * i], c[2 * i + 1]}) < 1e-12, qPrintable(QStringLiteral("row %1 point %2").arg(row).arg(i)));
        seen |= 1 << row;
    }
    QCOMPARE(seen, 15);
}

// A spline-fit polyline (PEDIT Spline): its curve goes through every vertex AutoCAD worked
// out for it (flag 8), from the frame vertices (flag 16).
void TestCorpus::splineFitPolylineMatchesAutoCad() {
    auto d = loadFixture("spl_pline");
    QVERIFY(d);
    const Primitive* pl = nullptr;
    for (const auto& p : d->prims)
        if (p.kind == PrimKind::Spline && p.spline->from == SplineFrom::Polyline) pl = &p;
    QVERIFY(pl);
    QCOMPARE(pl->spline->degree, 3);
    QFile f(fixture(QStringLiteral("spl_pline")));
    QVERIFY(f.open(QIODevice::ReadOnly));
    int computed = 0;
    for (const auto& v : QJsonDocument::fromJson(f.readAll()).object().value("OBJECTS").toArray()) {
        const QJsonObject o = v.toObject();
        if (o.value("entity").toString() != QLatin1String("VERTEX_2D") || !(o.value("flag").toInt() & 8)) continue;
        const QJsonArray q = o.value("point").toArray();
        QVERIFY(distanceToSpline(*pl->spline, {q[0].toDouble(), q[1].toDouble()}) < 1e-12);
        ++computed;
    }
    QCOMPARE(computed, 17);
}

// Where they were drawn: the same file, the top edge drawn the other way, the part as a block
// at half size inserted at scale 2, and both splines given by AutoCAD's own control points.
void TestCorpus::splinesMatchInPlace_data() {
    QTest::addColumn<QString>("name");
    for (const char* n : {"spl_ref", "spl_reversed", "spl_block", "spl_cv"}) QTest::newRow(n) << QString::fromLatin1(n);
}

void TestCorpus::splinesMatchInPlace() {
    QFETCH(QString, name);
    auto r0 = loadFixture("spl_ref");
    auto d = loadFixture(name);
    QVERIFY(r0 && d);
    const CompareResult r = compareDrawings(*r0, *d, absolute(), nullptr);
    QVERIFY2(r.pass, qPrintable(dumpErrors(r)));
    QCOMPARE(r.matched, 5);
}

void TestCorpus::splinesMatchMoved_data() {
    QTest::addColumn<QString>("name");
    QTest::addColumn<bool>("mirrored");
    QTest::newRow("translated") << QStringLiteral("spl_translated") << false;
    QTest::newRow("rotated37") << QStringLiteral("spl_rotated37") << false;
    QTest::newRow("mirrored") << QStringLiteral("spl_mirrored") << true;
}

void TestCorpus::splinesMatchMoved() {
    QFETCH(QString, name);
    QFETCH(bool, mirrored);
    auto r0 = loadFixture("spl_ref");
    auto d = loadFixture(name);
    QVERIFY(r0 && d);
    CompareSettings s = relative();
    s.allowMirror = false;
    CompareResult r = compareDrawings(*r0, *d, s, nullptr);
    QCOMPARE(r.pass, !mirrored);
    if (mirrored) {
        s.allowMirror = true;
        r = compareDrawings(*r0, *d, s, nullptr);
        QVERIFY(r.mirrored);
    }
    QVERIFY2(r.pass, qPrintable(dumpErrors(r)));
    QCOMPARE(r.matched, 5);
}

void TestCorpus::splineNearMisses_data() {
    QTest::addColumn<QString>("name");
    QTest::addColumn<QString>("label");
    QTest::addColumn<QString>("start");
    QTest::addColumn<QString>("text");
    QTest::newRow("pointoff") << QStringLiteral("spl_pointoff") << QStringLiteral("wrong points") << QStringLiteral("spline through 5 points at (")
                              << QStringLiteral(": point 3 drawn at (3.0, 2.7), expected (3.0, 2.8)");
    QTest::newRow("fewer") << QStringLiteral("spl_fewer") << QStringLiteral("wrong points") << QStringLiteral("spline through 5 points at (")
                           << QStringLiteral(" is drawn through 4 points, expected 5");
    QTest::newRow("tangent") << QStringLiteral("spl_tangent") << QStringLiteral("wrong shape") << QStringLiteral("spline through 5 points at (")
                             << QStringLiteral(" goes through the right points but is off by up to 0.256");
    QTest::newRow("pline") << QStringLiteral("spl_pline") << QStringLiteral("wrong shape") << QStringLiteral("spline through 5 points at (")
                           << QStringLiteral(" is off by up to 0.275");
    QTest::newRow("shifted") << QStringLiteral("spl_shifted") << QStringLiteral("shifted") << QStringLiteral("spline through 4 points at (")
                             << QStringLiteral(" is shifted by (0.1, 0.0)");
}

void TestCorpus::splineNearMisses() {
    QFETCH(QString, name);
    QFETCH(QString, label);
    QFETCH(QString, start);
    QFETCH(QString, text);
    auto r0 = loadFixture("spl_ref");
    auto d = loadFixture(name);
    QVERIFY(r0 && d);
    const CompareResult r = compareDrawings(*r0, *d, absolute(), nullptr);
    QVERIFY(!r.pass);
    QCOMPARE(r.refCount, 5);
    QCOMPARE(r.matched, 4);
    QCOMPARE(r.penalizedExtras, 0);
    int near = 0;
    for (const auto& e : r.errors) {
        if (e.cat != ErrCat::NearMiss) continue;
        ++near;
        QCOMPARE(e.label, label);
        QVERIFY2(e.text.startsWith(start) && e.text.contains(text), qPrintable(e.text));
    }
    QCOMPARE(near, 1);
}

// A fit tolerance makes AutoCAD approximate the points: not rebuilt, so reported and left out.
void TestCorpus::splineWithFitToleranceIsNotGraded() {
    auto r0 = loadFixture("spl_ref");
    auto d = loadFixture("spl_tol");
    QVERIFY(r0 && d);
    QCOMPARE(countKind(*d, PrimKind::Unsupported), 1);
    const CompareResult r = compareDrawings(*r0, *d, absolute(), nullptr);
    QCOMPARE(r.matched, 4);
    QVERIFY(anyError(r, QStringLiteral("missing spline through 5 points")));
    QVERIFY(anyError(r, QStringLiteral("SPLINE (fit tolerance) x1")));
}

// The lineweight set (tests/corpus, D100): four lines 4 long at y = 0..3, on layer THICK
// (0.50 mm), by layer on OBJECT (Default), 0.35 mm on the line, and in a block inserted at
// 0.70 mm.
void TestCorpus::lineweightsRead() {
    auto d = loadFixture("lw_ref");
    QVERIFY(d);
    struct Want { double y; int lw; bool byLayer; };
    for (const Want& w : {Want{0, 50, true}, Want{1, kLineweightDefault, true}, Want{2, 35, false}, Want{3, 70, false}}) {
        const Primitive* p = findLine(*d, {0, w.y}, {4, w.y});
        QVERIFY2(p, qPrintable(QString::number(w.y)));
        QCOMPARE(p->lineweight, w.lw);
        QCOMPARE(p->lwByLayer, w.byLayer);
    }
    bool thick = false;
    for (const auto& l : d->layers) thick = thick || (l.name == QLatin1String("THICK") && l.lineweight == 50);
    QVERIFY(thick);
    QCOMPARE(lineweightName(50), QStringLiteral("0.50 mm"));
    QCOMPARE(lineweightName(kLineweightDefault), QStringLiteral("Default (0.25 mm)"));
}

void TestCorpus::lineweightsMustMatch() {
    auto r0 = loadFixture("lw_ref"), wrong = loadFixture("lw_wrong"), bylayer = loadFixture("lw_bylayer");
    QVERIFY(r0 && wrong && bylayer);
    CompareSettings s = absolute();
    QVERIFY(compareDrawings(*r0, *wrong, s, nullptr).pass);  // not counted unless asked for
    s.matchLineweight = true;
    // The first line is 0.25 mm on the object, not 0.50 mm. The second is 0.25 mm on the object
    // where the solution's is Default, which counts as 0.25 mm.
    const CompareResult r = compareDrawings(*r0, *wrong, s, nullptr);
    QCOMPARE(r.matched, 3);
    QCOMPARE(countLabel(r, QStringLiteral("wrong lineweight")), 1);
    QVERIFY2(anyError(r, QStringLiteral("visible line 4.000 long at (2.0, 0.0) has lineweight 0.25 mm, expected 0.50 mm")),
             qPrintable(dumpErrors(r)));
    QVERIFY2(compareDrawings(*r0, *bylayer, s, nullptr).pass, qPrintable(dumpErrors(compareDrawings(*r0, *bylayer, s, nullptr))));
}

void TestCorpus::lineweightSetByLayer() {
    auto bylayer = loadFixture("lw_bylayer"), r0 = loadFixture("lw_ref"), wrong = loadFixture("lw_wrong");
    QVERIFY(bylayer && r0 && wrong);
    CompareSettings s = absolute();
    s.requireByLayer = true;
    QVERIFY2(compareDrawings(*bylayer, *bylayer, s, nullptr).pass, "every lineweight comes from its layer");
    // Graded against the by-layer version, the reference has two set on the objects: the
    // 0.35 mm line, and the block inserted at 0.70 mm.
    const CompareResult r = compareDrawings(*bylayer, *r0, s, nullptr);
    QCOMPARE(countLabel(r, QStringLiteral("not by layer")), 2);
    QVERIFY2(anyError(r, QStringLiteral("has its lineweight set on the object, not by layer")), qPrintable(dumpErrors(r)));
    // With lineweights graded too: one wrong lineweight, three set on the objects.
    s.matchLineweight = true;
    const CompareResult w = compareDrawings(*bylayer, *wrong, s, nullptr);
    QCOMPARE(countLabel(w, QStringLiteral("wrong lineweight")), 1);
    QCOMPARE(countLabel(w, QStringLiteral("not by layer")), 3);
}

// The readability set (tests/corpus, D101): a 6 x 3 plate (in model view 1000 px across: 166.7
// px per inch) with a long hidden line, a hidden line 0.25 long (its pattern is 0.375), a
// center line at linetype scale 0.05 (gaps 0.0125), squares hatched ANSI31 at scale 0.1
// (lines 0.0125 apart), 2 (a 0.3 square: 1.7 lines across) and 1, and dimensions with text
// 0.18 and 0.04, both with arrows 0.18. Layout1 shows it all at 1:2 (inches).
void TestCorpus::readabilitySizesRead() {
    auto d = loadFixture("rd_ref");
    QVERIFY(d);
    const Primitive* shortHidden = findLine(*d, {1, 2}, {1.25, 2});
    const Primitive* center = findLine(*d, {0, 2.5}, {6, 2.5});
    QVERIFY(shortHidden && center);
    QVERIFY(std::fabs(shortHidden->ltPattern - 0.375) < 1e-12 && std::fabs(shortHidden->ltGap - 0.125) < 1e-12);
    QVERIFY(std::fabs(center->ltPattern - 0.1) < 1e-12 && std::fabs(center->ltGap - 0.0125) < 1e-12);
    std::vector<double> spacings, texts, arrows;
    for (const auto& p : d->prims) {
        if (p.kind == PrimKind::Hatch && p.hatchLines) spacings.push_back(p.hatchLines->front().spacing);
        if (p.kind == PrimKind::Dimension) {
            texts.push_back(p.dimTextHeight);
            arrows.push_back(p.dimArrowSize);
        }
    }
    std::sort(spacings.begin(), spacings.end());
    std::sort(texts.begin(), texts.end());
    QCOMPARE(spacings.size(), size_t(3));
    QVERIFY(std::fabs(spacings[0] - 0.0125) < 1e-9 && std::fabs(spacings[1] - 0.125) < 1e-9 && std::fabs(spacings[2] - 0.25) < 1e-9);
    QCOMPARE(texts.size(), size_t(2));
    QVERIFY(std::fabs(texts[0] - 0.04) < 1e-9 && std::fabs(texts[1] - 0.18) < 1e-9);
    for (double a : arrows) QVERIFY(std::fabs(a - 0.18) < 1e-9);
    QCOMPARE(d->viewports.size(), size_t(1));
    QVERIFY(std::fabs(d->viewports[0].scale - 0.5) < 1e-9);
    QCOMPARE(d->viewports[0].mmPerUnit, 25.4);
    QVERIFY(d->viewports[0].window.contains({-0.6, 1.5}) && d->viewports[0].window.contains({6, 3}));
    QVERIFY(d->psltscale);
}

void TestCorpus::readabilityLevels_data() {
    QTest::addColumn<bool>("paper");
    QTest::addColumn<int>("level");
    QTest::addColumn<QStringList>("labels");
    const QString cont = QStringLiteral("dashes don't show"), gaps = QStringLiteral("dashes too small"),
                  dense = QStringLiteral("hatch too dense"), sparse = QStringLiteral("hatch too sparse"),
                  small = QStringLiteral("text too small"), arrows = QStringLiteral("arrows out of proportion");
    // Model view: the center line's gaps are 2.1 px, the dense hatch's lines 2.1 px apart, the
    // small text 6.7 px high; the short line holds its pattern 0.67 times, the sparse hatch has
    // 1.7 lines across, the small text's arrows are 4.5 times its height.
    QTest::newRow("model loose") << false << 0 << QStringList{cont, sparse, arrows};
    QTest::newRow("model medium") << false << 1 << QStringList{cont, dense, sparse, small, arrows};
    QTest::newRow("model strict") << false << 2 << QStringList{cont, gaps, dense, sparse, small, arrows};
    // On paper at 1:2: gaps 0.32 mm (PSLTSCALE keeps them), hatch lines 0.16 and 1.59 mm apart,
    // text 2.29 and 0.51 mm; the short line holds a pattern twice as long, 0.33 times.
    QTest::newRow("paper loose") << true << 0 << QStringList{cont, dense, sparse, small, arrows};
    QTest::newRow("paper medium") << true << 1 << QStringList{cont, gaps, dense, sparse, small, arrows};
    QTest::newRow("paper strict") << true << 2 << QStringList{cont, gaps, dense, sparse, small, arrows};
}

void TestCorpus::readabilityLevels() {
    QFETCH(bool, paper);
    QFETCH(int, level);
    QFETCH(QStringList, labels);
    auto d = loadFixture("rd_ref");
    QVERIFY(d);
    ReadOptions o;
    o.dashes = o.hatches = o.dimensions = true;
    o.strictness = static_cast<ReadStrictness>(level);
    o.onPaper = paper;
    QStringList got;
    QHash<QString, int> count;
    for (const ReadIssue& i : readabilityIssues(*d, o)) {
        got << i.label;
        count[i.label] = static_cast<int>(i.prims.size());
    }
    QCOMPARE(got, labels);
    if (paper && level == 2) QCOMPARE(count.value(QStringLiteral("hatch too dense")), 2);  // 1.59 mm too: under 2
    if (paper && level >= 1) QCOMPARE(count.value(QStringLiteral("text too small")), 2);   // 2.29 mm too: under 2.5
    // Only the checks asked for.
    o.hatches = o.dimensions = false;
    for (const ReadIssue& i : readabilityIssues(*d, o)) QVERIFY2(i.label.startsWith(QStringLiteral("dashes")), qPrintable(i.label));
}

void TestCorpus::readabilityMessages() {
    auto d = loadFixture("rd_ref");
    QVERIFY(d);
    ReadOptions o;
    o.dashes = o.hatches = o.dimensions = true;
    QStringList model;
    for (const ReadIssue& i : readabilityIssues(*d, o)) model << i.text;
    QVERIFY2(model.contains(QStringLiteral("1 dashed line looks continuous: it is too short to hold its dash pattern once "
                                           "(hidden line 0.250 long at (1.125, 2.0) holds it 0.67 times)")),
             qPrintable(model.join(QStringLiteral("\n"))));
    QVERIFY2(model.contains(QStringLiteral("1 hatch looks hardly hatched: as few as 1.7 lines across the region, at least 3 needed "
                                           "(hatch ANSI31 at (4.15, 0.35))")),
             qPrintable(model.join(QStringLiteral("\n"))));
    o.onPaper = true;
    o.strictness = ReadStrictness::Strict;
    QStringList paper;
    for (const ReadIssue& i : readabilityIssues(*d, o)) paper << i.text;
    bool gaps = false, dense = false;
    for (const QString& t : paper) {
        gaps = gaps || t.startsWith(QStringLiteral("1 dashed line looks solid: gaps as short as 0.32 mm on paper, at least 1 mm needed"));
        dense = dense || t.startsWith(QStringLiteral("2 hatches look solid: lines as close as 0.16 mm on paper, at least 2 mm apart "
                                                     "needed (worst: hatch ANSI31 at (2.5, 0.7))"));
    }
    QVERIFY2(gaps && dense, qPrintable(paper.join(QStringLiteral("\n"))));
    // No layout viewport: judged in model view, and said so.
    auto plain = loadFixture("spl_ref");
    QVERIFY(plain && plain->viewports.empty());
    const auto notes = readabilityIssues(*plain, o);
    QVERIFY(!notes.empty() && notes.front().label.isEmpty() && notes.front().text.startsWith(QStringLiteral("no layout viewport")));
}

void TestCorpus::readabilityInResults() {
    auto d = loadFixture("rd_ref");
    QVERIFY(d);
    CompareSettings s = absolute();
    s.readDashes = s.readHatches = s.readDimensions = true;
    CompareResult r = compareDrawings(*d, *d, s, nullptr);
    QVERIFY2(r.pass, qPrintable(dumpErrors(r)));  // warnings: listed, not counted
    QCOMPARE(r.correctness, 100.0);
    int warnings = 0;
    for (const auto& e : r.errors) warnings += e.cat == ErrCat::Readability && !e.failing && e.text.startsWith(QStringLiteral("warning: "));
    QCOMPARE(warnings, 5);
    s.readAsErrors = true;
    r = compareDrawings(*d, *d, s, nullptr);
    QVERIFY(!r.pass);
    QCOMPARE(r.correctness, 100.0);  // the score is the drawing's; readability fails it, as a misaligned view does
    QCOMPARE(countLabel(r, QStringLiteral("text too small")), 1);
}

// Sizes too large (D102), in rd_big: a 6 x 3 plate with a hidden line at linetype scale 3
// (gaps 0.375: 62.5 px in model view, 9.5 mm on paper at 1:2), a square hatched ANSI31 at scale
// 4 (lines 0.5 apart: 83 px, 6.35 mm) and a dimension with text 0.7 (117 px, 8.9 mm).
void TestCorpus::readabilityUpperLimits_data() {
    QTest::addColumn<bool>("paper");
    QTest::addColumn<int>("level");
    QTest::addColumn<QStringList>("labels");
    const QString gaps = QStringLiteral("dashes too long"), coarse = QStringLiteral("hatch too coarse"),
                  large = QStringLiteral("text too large");
    QTest::newRow("model loose") << false << 0 << QStringList{};
    QTest::newRow("model medium") << false << 1 << QStringList{coarse, large};
    QTest::newRow("model strict") << false << 2 << QStringList{gaps, coarse, large};
    QTest::newRow("paper loose") << true << 0 << QStringList{gaps};
    QTest::newRow("paper medium") << true << 1 << QStringList{gaps, coarse, large};
    QTest::newRow("paper strict") << true << 2 << QStringList{gaps, coarse, large};
}

void TestCorpus::readabilityUpperLimits() {
    QFETCH(bool, paper);
    QFETCH(int, level);
    QFETCH(QStringList, labels);
    auto d = loadFixture("rd_big");
    QVERIFY(d);
    ReadOptions o;
    o.dashes = o.hatches = o.dimensions = true;
    o.strictness = static_cast<ReadStrictness>(level);
    o.onPaper = paper;
    QStringList got, texts;
    for (const ReadIssue& i : readabilityIssues(*d, o)) {
        got << i.label;
        texts << i.text;
    }
    QCOMPARE(got, labels);
    if (!paper || level != 1) return;
    const QString all = texts.join(QStringLiteral("\n"));
    QVERIFY2(texts.size() == 3 && texts[0].startsWith(QStringLiteral("1 dashed line looks broken up: gaps as long as 9.5")) &&
                 texts[0].contains(QStringLiteral(" mm on paper, at most 6 mm (hidden line 6.000 long at (3.0, 2.8))")),
             qPrintable(all));
    QVERIFY2(texts[1] == QStringLiteral("1 hatch looks too coarse: lines as far apart as 6.35 mm on paper, at most 6 mm "
                                        "(hatch ANSI31 at (3.0, 1.5))"),
             qPrintable(all));
    QVERIFY2(texts[2].startsWith(QStringLiteral("1 dimension has text too large: as large as 8.89 mm high on paper, at most 7 mm "
                                                "(dimension 6.000 at (")),
             qPrintable(all));
}

QTEST_GUILESS_MAIN(TestCorpus)
#include "tst_corpus.moc"
