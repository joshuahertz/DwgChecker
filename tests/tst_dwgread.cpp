// End to end with the real parser: runs the dwgread that CMake bundles
// (DWGCHECK_DWGREAD) on the corpus DWGs. Skipped when no dwgread is configured.

#include "Batch.h"
#include "Csv.h"
#include "TestUtil.h"

#include <QDir>
#include <QTemporaryDir>
#include <QtTest>

#ifndef DWGCHECK_DWGREAD_PATH
#define DWGCHECK_DWGREAD_PATH ""
#endif
#ifndef DWGCHECK_CORPUS
#define DWGCHECK_CORPUS "tests/corpus/dwg"
#endif

using namespace dc;
using namespace dct;

namespace {

LoaderSettings loaderSettings() {
    LoaderSettings l;
    l.dwgreadPath = QStringLiteral(DWGCHECK_DWGREAD_PATH);
    l.timeoutSec = 60;
    return l;
}
QString corpus(const QString& name) { return QStringLiteral(DWGCHECK_CORPUS "/") + name + ".dwg"; }

bool samePrimitive(const Primitive& a, const Primitive& b, QString* why) {
    const double tol = 1e-9;  // fixtures carry 14 decimals; a patched dwgread carries 17 digits
    auto fail = [&](const QString& w) { *why = w; return false; };
    if (a.kind != b.kind) return fail("kind");
    if (a.layer != b.layer || a.ltClass != b.ltClass || a.text != b.text) return fail("attributes");
    if (dist(a.p0, b.p0) > tol || dist(a.p1, b.p1) > tol || dist(a.center, b.center) > tol) return fail("points");
    if (std::fabs(a.radius - b.radius) > tol || angleDiff(a.a0, b.a0) > tol || angleDiff(a.a1, b.a1) > tol) return fail("arc");
    if (std::fabs(a.measurement - b.measurement) > tol) return fail("measurement");
    if (a.dimUnit != b.dimUnit || a.dimPrec != b.dimPrec || a.dimZin != b.dimZin) return fail("dimension format");
    if (static_cast<bool>(a.loops) != static_cast<bool>(b.loops)) return fail("loops");
    if (a.loops) {
        if (a.loops->size() != b.loops->size()) return fail("loop count");
        for (size_t i = 0; i < a.loops->size(); ++i) {
            if ((*a.loops)[i].size() != (*b.loops)[i].size()) return fail("loop size");
            for (size_t k = 0; k < (*a.loops)[i].size(); ++k)
                if (dist((*a.loops)[i][k], (*b.loops)[i][k]) > tol) return fail("loop point");
        }
    }
    return true;
}

}  // namespace

class TestDwgRead : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void pinnedVersion();
    void matchesFixtures_data();
    void matchesFixtures();
    void batchEndToEnd();
};

void TestDwgRead::initTestCase() {
    if (QStringLiteral(DWGCHECK_DWGREAD_PATH).isEmpty())
        QSKIP("no dwgread configured (set DWGCHECK_DWGREAD or run scripts/build_dwgread.sh)");
    QVERIFY2(QFileInfo::exists(QStringLiteral(DWGCHECK_DWGREAD_PATH)), DWGCHECK_DWGREAD_PATH);
}

void TestDwgRead::pinnedVersion() {
    QString err;
    const QString v = DwgReadLoader::probeVersion(QStringLiteral(DWGCHECK_DWGREAD_PATH), &err);
    QVERIFY2(!v.isEmpty(), qPrintable(err));
    // The bundled binary must be the version pinned in third_party/libredwg.version.
    QVERIFY2(v.startsWith(QStringLiteral("dwgread " DWGCHECK_LIBREDWG_VERSION " ")), qPrintable(v));
}

void TestDwgRead::matchesFixtures_data() {
    QTest::addColumn<QString>("name");
    const QStringList dwgs = QDir(QStringLiteral(DWGCHECK_CORPUS)).entryList({QStringLiteral("*.dwg")}, QDir::Files, QDir::Name);
    for (const QString& f : dwgs) {
        const QString n = QFileInfo(f).completeBaseName();
        // kitchen*: a patched dwgread (0001) explodes the MINSERT that the fixture marks unsupported.
        if (n.startsWith(QStringLiteral("kitchen"))) continue;
        QTest::newRow(qPrintable(n)) << n;
    }
}

void TestDwgRead::matchesFixtures() {
    QFETCH(QString, name);
    const DwgReadLoader loader(loaderSettings());
    QString err;
    auto live = loadDrawing(corpus(name), loader, CanonSettings{}, &err);
    QVERIFY2(live, qPrintable(err));
    auto fix = loadFixture(name);
    QVERIFY(fix);
    QCOMPARE(live->meta.dwgVersion, fix->meta.dwgVersion);
    QCOMPARE(live->meta.fingerprintGuid, fix->meta.fingerprintGuid);
    QCOMPARE(live->meta.versionGuid, fix->meta.versionGuid);
    QVERIFY(live->meta.tducreate == fix->meta.tducreate);
    QCOMPARE(live->meta.insUnits, fix->meta.insUnits);
    QCOMPARE(live->meta.lastSavedBy, fix->meta.lastSavedBy);
    QCOMPARE(live->prims.size(), fix->prims.size());
    for (size_t i = 0; i < live->prims.size(); ++i) {
        QString why;
        QVERIFY2(samePrimitive(live->prims[i], fix->prims[i], &why),
                 qPrintable(QStringLiteral("primitive %1 (%2): %3").arg(i).arg(fix->prims[i].sourceId, why)));
    }
}

void TestDwgRead::batchEndToEnd() {
    // A real batch through the bundled parser, including a file that cannot be read.
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString broken = tmp.filePath("brokenstudent_1_2_lab.dwg");
    QFile f(broken);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("this is not a DWG file");
    f.close();

    BatchInput in;
    in.refPaths = {corpus("ref")};
    in.studentPaths = {corpus("ref_rotated37"), corpus("ref_missing"), corpus("ref_bytecopy"), broken};
    in.settings.loader = loaderSettings();
    in.settings.compare.mode = Mode::Relative;
    const AutoLoader loader(in.settings.loader);
    const BatchOutput out = runBatch(in, loader);
    QVERIFY2(out.ok, qPrintable(out.error));
    QCOMPARE(out.students.size(), size_t(4));
    QVERIFY(out.students[0].ok() && out.students[0].result.pass);  // rotated 37 degrees
    QVERIFY(out.students[1].ok());
    QCOMPARE(out.students[1].result.correctness, 90.0);           // one hole missing
    QVERIFY(!out.students[3].ok());                                // per-file error, batch continued
    QVERIFY(!out.students[3].loadError.isEmpty());
    const CopyFlag flag = copyFlagFor(out, 2);                     // byte copy of the reference
    QCOMPARE(flag.level, Strength::Strong);
    QVERIFY(out.parser.startsWith(QStringLiteral("dwgread ")));
    QVERIFY(resultsCsv(out).contains(out.parser));     // parser version in every CSV export
}

QTEST_GUILESS_MAIN(TestDwgRead)
#include "tst_dwgread.moc"
