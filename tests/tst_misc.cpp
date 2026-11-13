// Geometry, matching, alignment, identity, formatting, settings and CSV.

#include "Aligner.h"
#include "Archive.h"
#include "Batch.h"
#include "ClassSummary.h"
#include "Color.h"
#include "Comparator.h"
#include "Csv.h"
#include "Format.h"
#include "Matcher.h"
#include "Settings.h"
#include "StudentIdentity.h"
#include "TestUtil.h"

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
    QVERIFY(t.compare.matchColor && t.compare.requireByLayer);
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
