// End-to-end tests on the real corpus (tests/corpus/dwg, dumped to tests/fixtures
// with dwgread 0.14). Covers loading/metadata (M1), canonicalization on real
// files (M2), Absolute (M3) and Relative (M4) comparison.

#include "Comparator.h"
#include "MetaExtractor.h"
#include "TestUtil.h"

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
    // Unsupported: SPLINE, ELLIPSE, XLINE, MINSERT (no counts in 0.14 JSON), plus the circle
    // and arc of the non-uniformly scaled insert.
    QCOMPARE(countKind(*d, PrimKind::Unsupported), 6);
    QVERIFY(hasWarning(*d, WarnKind::Unsupported));
    QVERIFY(hasWarning(*d, WarnKind::NonUniformScale));
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

QTEST_GUILESS_MAIN(TestCorpus)
#include "tst_corpus.moc"
