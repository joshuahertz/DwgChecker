// Whole batches on the JSON fixtures: grouping files by student, matching each
// student's files one to one to several solution drawings, extra and missing files, copy
// detection per part, and the CSV export. Student files are fixture copies with
// Canvas-style names (name_userid_attachmentid_original).

#include "Batch.h"
#include "Csv.h"
#include "TestUtil.h"

#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest>

using namespace dc;
using namespace dct;

namespace {

BatchInput absoluteBatch() {
    BatchInput in;
    in.settings.compare.mode = Mode::Absolute;
    in.settings.compare.detectScale = false;  // plain scores, so the matching is easy to predict
    return in;
}

// A 4 x 2 rectangle at (10, 10) on layer 0.
void addPart(DocBuilder& b) {
    b.line({10, 10}, {14, 10});
    b.line({14, 10}, {14, 12});
    b.line({14, 12}, {10, 12});
    b.line({10, 12}, {10, 10});
}

int fileNamed(const BatchOutput& out, const QString& name) {
    for (int i = 0; i < static_cast<int>(out.students.size()); ++i)
        if (out.students[i].identity.fileName == name) return i;
    return -1;
}

}  // namespace

class TestBatch : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void severalSolutionDrawings();
    void oneSolutionDrawingGroupsByStudent();
    void modePerDrawing();
    void perDrawingSettingsRoundTrip();
    void turnedOffLayersInSolutionIgnored();
    void absoluteDrawingInWrongPlaceFindsItsDrawing();
    void oneToOneForTheBestTotal();

private:
    QTemporaryDir m_dir;
    QStringList m_students;
    QString writeAs(const DocBuilder& b, const QString& as) {
        const QString to = m_dir.filePath(as);
        QFile f(to);
        if (!f.open(QIODevice::WriteOnly)) qWarning("write %s failed", qPrintable(as));
        f.write(QJsonDocument(b.root()).toJson(QJsonDocument::Compact));
        return to;
    }
    QString copyAs(const QString& fixtureName, const QString& as) {
        const QString to = m_dir.filePath(as);
        QFile::remove(to);
        if (!QFile::copy(fixture(fixtureName), to)) qWarning("copy %s failed", qPrintable(as));
        return to;
    }
};

void TestBatch::initTestCase() {
    QVERIFY(m_dir.isValid());
    // smithjane: both parts (part A with a hole missing).
    m_students << copyAs("ref_missing", "smithjane_101_201_partA.json") << copyAs("ortho_ref", "smithjane_101_202_partB.json");
    // doejohn: part A twice (exact, and with an extra line), no part B.
    m_students << copyAs("ref", "doejohn_102_203_x.json") << copyAs("ref_extra", "doejohn_102_204_y.json");
    // lee: one unreadable file.
    const QString broken = m_dir.filePath("lee_103_205_z.json");
    QFile f(broken);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("this is not JSON");
    f.close();
    m_students << broken;
}

void TestBatch::severalSolutionDrawings() {
    BatchInput in = absoluteBatch();
    in.refPaths = {fixture("ref"), fixture("ortho_ref")};
    in.studentPaths = m_students;
    const JsonFileLoader loader;
    const BatchOutput out = runBatch(in, loader);
    QVERIFY2(out.ok, qPrintable(out.error));
    QVERIFY(out.multipleRefs());
    QCOMPARE(out.students.size(), size_t(5));
    QCOMPARE(out.groups.size(), size_t(3));  // sorted by name

    const StudentGroup& doe = out.groups[0];
    QCOMPARE(doe.identity.name, QStringLiteral("doejohn"));
    QCOMPARE(doe.byRef.size(), size_t(2));
    QCOMPARE(doe.byRef[0], fileNamed(out, "doejohn_102_203_x.json"));  // the exact copy wins part A
    // One to one for the best total: two files, two drawings, so the second copy of part A
    // is graded as part B (and scores next to nothing there).
    const int yFile = fileNamed(out, "doejohn_102_204_y.json");
    QCOMPARE(doe.byRef[1], yFile);
    const StudentRun& y = out.students[yFile];
    QVERIFY(y.ok() && y.matched());
    QCOMPARE(y.closestRef, 0);  // still closest to part A
    QVERIFY(y.result.correctness < 20);
    QVERIFY(qFuzzyCompare(doe.correctness, (100.0 + y.result.correctness) / 2));

    const StudentGroup& lee = out.groups[1];
    QCOMPARE(lee.identity.name, QStringLiteral("lee"));
    QCOMPARE(lee.byRef, (std::vector<int>{-1, -1}));
    QVERIFY(!out.students[lee.files.front()].ok());
    QCOMPARE(lee.correctness, 0.0);

    const StudentGroup& smith = out.groups[2];
    QCOMPARE(smith.identity.name, QStringLiteral("smithjane"));
    const StudentRun& a = out.students[smith.byRef[0]];
    const StudentRun& b = out.students[smith.byRef[1]];
    QCOMPARE(a.identity.fileName, QStringLiteral("smithjane_101_201_partA.json"));
    QCOMPARE(b.identity.fileName, QStringLiteral("smithjane_101_202_partB.json"));
    QCOMPARE(a.result.correctness, 90.0);  // one hole missing
    QCOMPARE(b.result.correctness, 100.0);
    QVERIFY(qFuzzyCompare(smith.correctness, 95.0));
    QCOMPARE(&out.refDrawing(b), out.refs[1].get());

    // Copy detection: a student's own files are never paired; copies of the solution are.
    const int x = fileNamed(out, "doejohn_102_203_x.json"), yi = fileNamed(out, "doejohn_102_204_y.json");
    for (const PairResult& p : out.dups.pairs) QVERIFY(!((p.a == x && p.b == yi) || (p.a == yi && p.b == x)));
    QCOMPARE(copyFlagFor(out, x).level, Strength::Strong);  // byte copy of solution A

    const QString csv = resultsCsv(out);
    QVERIFY(csv.contains(QStringLiteral("Student,Solution drawing,File,Possible cheating,Correctness,Errors")));
    QVERIFY(csv.contains(QStringLiteral("doejohn,ortho_ref.json,doejohn_102_204_y.json,")));
    QVERIFY(csv.contains(QStringLiteral("lee,ref.json,,,0.0%,no file matched this solution drawing")));
    QVERIFY(csv.contains(QStringLiteral("lee,(extra file),lee_103_205_z.json")));
    QVERIFY(csv.contains(QStringLiteral("smithjane,(all drawings),,,95.0%")));
}

void TestBatch::oneSolutionDrawingGroupsByStudent() {
    BatchInput in = absoluteBatch();
    in.refPaths = {fixture("ref")};
    in.studentPaths = m_students;
    const JsonFileLoader loader;
    const BatchOutput out = runBatch(in, loader);
    QVERIFY2(out.ok, qPrintable(out.error));
    QVERIFY(!out.multipleRefs());
    QCOMPARE(out.groups.size(), size_t(3));
    const StudentGroup& smith = out.groups[2];
    QCOMPARE(smith.byRef.size(), size_t(1));
    QCOMPARE(out.students[smith.byRef[0]].identity.fileName, QStringLiteral("smithjane_101_201_partA.json"));
    QVERIFY(!out.students[fileNamed(out, "smithjane_101_202_partB.json")].matched());  // not graded
    QVERIFY(qFuzzyCompare(smith.correctness, 90.0));
    QCOMPARE(correctnessText(out.students[fileNamed(out, "smithjane_101_202_partB.json")]), QStringLiteral("not graded"));
    // A single solution drawing keeps the one-row-per-file CSV.
    QVERIFY(resultsCsv(out).contains(QStringLiteral("File,Possible cheating,Correctness,Errors")));
}

void TestBatch::modePerDrawing() {
    // Part A in Absolute mode, part B (three views) in Orthographic mode.
    BatchInput in = absoluteBatch();
    in.refPaths = {fixture("ref"), fixture("ortho_ref")};
    in.studentPaths = {m_students[0], m_students[1]};  // smithjane's two parts
    in.settings.perDrawing[QStringLiteral("ortho_ref.json")] = DrawingSettings{Mode::Orthographic, false, false};
    const JsonFileLoader loader;
    const BatchOutput out = runBatch(in, loader);
    QVERIFY2(out.ok, qPrintable(out.error));
    QCOMPARE(out.refModes, (std::vector<Mode>{Mode::Absolute, Mode::Orthographic}));
    QVERIFY(out.hasViews);
    QVERIFY(out.views[0].views.empty());
    QCOMPARE(out.views[1].views.size(), size_t(3));
    const StudentGroup& smith = out.groups[0];
    const StudentRun& a = out.students[smith.byRef[0]];
    const StudentRun& b = out.students[smith.byRef[1]];
    QCOMPARE(a.result.mode, Mode::Absolute);
    QCOMPARE(a.result.correctness, 90.0);
    QVERIFY(!out.viewsOf(a));
    QCOMPARE(b.result.mode, Mode::Orthographic);
    QVERIFY2(b.result.pass, qPrintable(b.result.errorStrings().join(" | ")));
    QCOMPARE(b.result.views.size(), size_t(3));
    QVERIFY(out.viewsOf(b));
    QVERIFY(resultsCsv(out).contains(QStringLiteral("# mode: ref.json absolute, ortho_ref.json orthographic")));
}

void TestBatch::perDrawingSettingsRoundTrip() {
    AppSettings s;
    s.compare.mode = Mode::Relative;
    s.perDrawing[AppSettings::drawingKey(QStringLiteral("C:/quiz/Part B.dwg"))] = DrawingSettings{Mode::Orthographic, true, true};
    const AppSettings t = AppSettings::fromJson(s.toJson());
    QCOMPARE(t.perDrawing.size(), size_t(1));
    const CompareSettings b = t.compareFor(QStringLiteral("D:/elsewhere/PART B.DWG"));  // by file name, any case
    QCOMPARE(b.mode, Mode::Orthographic);
    QVERIFY(b.checkDimensions && b.checkHatches);
    const CompareSettings a = t.compareFor(QStringLiteral("C:/quiz/Part A.dwg"));  // not listed: the shared settings
    QCOMPARE(a.mode, Mode::Relative);
    QVERIFY(!a.checkDimensions);
}

void TestBatch::turnedOffLayersInSolutionIgnored() {
    // The solution has a sketching grid on a turned-off layer (flag0 bit 2) next to the part;
    // the student drew only the part.
    DocBuilder ref;
    ref.layer(QStringLiteral("GRID"), QStringLiteral("Continuous"), 16 + (31 << 5) + 2);
    for (int i = 0; i <= 5; ++i) {
        ref.line({double(i), 0}, {double(i), 5}, QStringLiteral("GRID"));
        ref.line({0, double(i)}, {5, double(i)}, QStringLiteral("GRID"));
    }
    addPart(ref);
    DocBuilder stu;
    addPart(stu);
    BatchInput in = absoluteBatch();
    in.refPaths = {writeAs(ref, QStringLiteral("gridref.json"))};
    in.studentPaths = {writeAs(stu, QStringLiteral("parkmin_104_206_part.json"))};
    const JsonFileLoader loader;

    QVERIFY(in.settings.canon.ignoreHiddenLayersInReference);  // the default
    BatchOutput out = runBatch(in, loader);
    QVERIFY2(out.ok, qPrintable(out.error));
    QCOMPARE(countKind(*out.refs[0], PrimKind::Line), 4);
    QCOMPARE(out.students[0].result.correctness, 100.0);
    QVERIFY(out.log.join('\n').contains(QStringLiteral("12 objects on turned-off or frozen layers (GRID) were ignored")));

    in.settings.canon.ignoreHiddenLayersInReference = false;  // the grid becomes part of the answer
    out = runBatch(in, loader);
    QVERIFY2(out.ok, qPrintable(out.error));
    QCOMPARE(countKind(*out.refs[0], PrimKind::Line), 16);
    QVERIFY(out.students[0].result.correctness < 50.0);

    // The setting is saved with the quiz; student drawings keep their own rule.
    const AppSettings t = AppSettings::fromJson(in.settings.toJson());
    QVERIFY(!t.canon.ignoreHiddenLayersInReference);
    AppSettings d;
    QVERIFY(d.referenceCanon().skipHiddenLayers);
    QVERIFY(!d.effectiveCanon().skipHiddenLayers);
}

void TestBatch::absoluteDrawingInWrongPlaceFindsItsDrawing() {
    // Part A is graded in Absolute mode, part B in Relative mode. The student drew part A
    // perfectly but 12 right and 9.5 up, so in place it scores 0% there; it shares one
    // 4-long line with part B, so by its in-place score it looked more like part B. A third file, part B again
    // with a circle missing, is the one left over.
    auto partA = [](DocBuilder& b, Vec2 o) {
        b.line({o.x, o.y}, {o.x + 4, o.y});
        b.line({o.x + 4, o.y}, {o.x + 4, o.y + 2});
        b.line({o.x + 4, o.y + 2}, {o.x, o.y + 2});
        b.line({o.x, o.y + 2}, {o.x, o.y});
        b.circle({o.x + 1, o.y + 1}, 0.5);
    };
    auto partB = [](DocBuilder& b) {
        b.line({0, 0}, {4, 0});
        for (double x : {0.0, 2.0, 4.0}) b.circle({x, 1}, 0.3);
    };
    DocBuilder ra, rb, sa, sb, sc;
    partA(ra, {0, 0});
    partB(rb);
    partA(sa, {12, 9.5});
    partB(sb);
    sc.line({0, 0}, {4, 0});
    for (double x : {0.0, 2.0}) sc.circle({x, 1}, 0.3);
    BatchInput in = absoluteBatch();
    in.refPaths = {writeAs(ra, QStringLiteral("parta.json")), writeAs(rb, QStringLiteral("partb.json"))};
    in.settings.perDrawing[QStringLiteral("parta.json")] = DrawingSettings{Mode::Absolute, false, false};
    in.settings.perDrawing[QStringLiteral("partb.json")] = DrawingSettings{Mode::Relative, false, false};
    in.studentPaths = {writeAs(sa, QStringLiteral("ngo_105_301_x.json")), writeAs(sb, QStringLiteral("ngo_105_302_y.json")),
                       writeAs(sc, QStringLiteral("ngo_105_303_z.json"))};
    const JsonFileLoader loader;
    const BatchOutput out = runBatch(in, loader);
    QVERIFY2(out.ok, qPrintable(out.error));
    QCOMPARE(out.groups.size(), size_t(1));
    const StudentGroup& g = out.groups[0];
    QCOMPARE(out.students[g.byRef[0]].identity.fileName, QStringLiteral("ngo_105_301_x.json"));
    QCOMPARE(out.students[g.byRef[1]].identity.fileName, QStringLiteral("ngo_105_302_y.json"));
    const CompareResult& a = out.students[g.byRef[0]].result;
    QCOMPARE(a.mode, Mode::Absolute);
    QVERIFY(a.moved);  // wrong location counts once (the default): 5 of 5, plus one error
    QVERIFY(qFuzzyCompare(a.correctness, 100.0 * 5 / 6));
    QCOMPARE(a.placementFreeCorrectness, 100.0);
    const QString all = a.errorStrings().join(QStringLiteral(" | "));
    QVERIFY2(all.contains(QStringLiteral("the whole drawing is shifted by (12.0, 9.5) from the reference; after moving it "
                                         "back, 5 of 5")),
             qPrintable(all));
    QCOMPARE(out.students[g.byRef[1]].result.placementFreeCorrectness, -1.0);  // Relative: not needed
    const StudentRun& z = out.students[fileNamed(out, "ngo_105_303_z.json")];
    QVERIFY(z.ok() && !z.matched());  // three files, two drawings
    QCOMPARE(z.closestRef, 1);
    QVERIFY(resultsCsv(out).contains(QStringLiteral("ngo,(extra file),ngo_105_303_z.json")));
}

void TestBatch::oneToOneForTheBestTotal() {
    // Greedy would give file 0 drawing 0 (its 60 is the largest score) and leave file 1 the 5
    // on drawing 1; the best total is 55 + 58.
    QCOMPARE(assignOneToOne({{60, 55}, {58, 5}}), (std::vector<int>{1, 0}));
    // The last file takes the last drawing, however poorly it fits.
    QCOMPARE(assignOneToOne({{0, 5.6, 2}, {0, 100, 3.3}, {0, 3.3, 100}}), (std::vector<int>{0, 1, 2}));
    // More files than drawings: the one the total misses least is left over.
    QCOMPARE(assignOneToOne({{100, 6}, {90.9, 5.9}, {10, 100}}), (std::vector<int>{0, -1, 1}));
    // Fewer files than drawings: each file still gets one; a drawing stays free.
    QCOMPARE(assignOneToOne({{0, 100, 3}, {2, 4, 100}}), (std::vector<int>{1, 2}));
    QCOMPARE(assignOneToOne({}), std::vector<int>{});
}

QTEST_GUILESS_MAIN(TestBatch)
#include "tst_batch.moc"
