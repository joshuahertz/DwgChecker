// Geometry, matching, alignment, identity, formatting, settings and CSV.

#include "Aligner.h"
#include "Comparator.h"
#include "Csv.h"
#include "Format.h"
#include "Matcher.h"
#include "Settings.h"
#include "StudentIdentity.h"
#include "TestUtil.h"

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
    void lineMatchEitherOrder();
    void arcMatchAngles();
    void looseCenterlines();
    void greedyOneToOne();
    void nearMissPairing();
    void supersetLine();
    void alignRecoversRigidTransform();
    void alignCircleOnlyDrawing();
    void alignMirror();
    void identityCanvas();
    void identityFallback();
    void personNames();
    void formatting();
    void settingsRoundTrip();
    void csvEscaping();
    void dimensionsChecked();
    void displacedCopiesArePaired();
    void displacedLineTurnedSlightly();
    void errorSummaryCounts();
    void similarityTransforms();
    void scaleCandidateList();
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

void TestMisc::personNames() {
    QVERIFY(personMatchesStudent("Jane Smith", "smithjane"));
    QVERIFY(personMatchesStudent("smithjane", "smithjane"));
    QVERIFY(!personMatchesStudent("jhertz", "smithjane"));
    QVERIFY(!personMatchesStudent("", "smithjane"));
}

void TestMisc::formatting() {
    QCOMPARE(fmtLen(0.75), QStringLiteral("0.750"));
    QCOMPARE(fmtLen(0.7501, 0.75), QStringLiteral("0.7501"));
    QCOMPARE(fmtCoord(3), QStringLiteral("3.0"));
    QCOMPARE(fmtCoord(0.125), QStringLiteral("0.125"));
    QCOMPARE(fmtCoord(-0.0000001), QStringLiteral("0.0"));
    QCOMPARE(fmtPt({3, 2.5}), QStringLiteral("(3.0, 2.5)"));
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
    const AppSettings t = AppSettings::fromJson(s.toJson());
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
    QVERIFY(d.compare.matchLinetype);
    QVERIFY(d.compare.allowRotation);
    QVERIFY(!d.compare.allowMirror);
    QCOMPARE(d.compare.linTol, 1e-4);
    QCOMPARE(d.compare.angTol, 1e-6);
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

QTEST_GUILESS_MAIN(TestMisc)
#include "tst_misc.moc"
