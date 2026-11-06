// Dimension checks ("check dimensions" and its precision sub-option) on the dim_*
// corpus. dim_ref uses style DC3: three decimal places and whole degrees. Each
// variant changes how some of its seven dimensions read (tests/corpus/README.md).
// Expected messages were cross-checked with an independent Python port on the
// same fixtures, and the formatter is checked against the text AutoCAD generated.

#include "Comparator.h"
#include "DimText.h"
#include "Settings.h"
#include "TestUtil.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QtTest>

using namespace dc;
using namespace dct;

namespace {

const QChar kDeg(0x00B0);

CompareSettings dimSettings(Mode m = Mode::Absolute) {
    CompareSettings s;
    s.mode = m;
    s.checkDimensions = true;  // the precision check is on by default
    return s;
}
QString dumpErrors(const CompareResult& r) { return r.errorStrings().join(" | "); }
int countCat(const CompareResult& r, ErrCat c) {
    int n = 0;
    for (const auto& e : r.errors) n += e.cat == c;
    return n;
}
int countFailing(const CompareResult& r) {
    int n = 0;
    for (const auto& e : r.errors) n += e.failing;
    return n;
}
const Primitive* dimOf(const Drawing& d, const QString& type, double meas) {
    for (const auto& p : d.prims)
        if (p.kind == PrimKind::Dimension && p.dimType == type && std::fabs(p.measurement - meas) < 1e-6) return &p;
    return nullptr;
}
Primitive synth(const QString& type, double meas, int unit, int prec, int zin = 0) {
    Primitive p;
    p.kind = PrimKind::Dimension;
    p.dimType = type;
    p.measurement = meas;
    p.dimUnit = unit;
    p.dimPrec = prec;
    p.dimZin = zin;
    return p;
}

// The text AutoCAD generated for each dimension (the MTEXT in its *D block), by dimension handle.
QHash<quint64, QString> autocadTexts(const QString& name) {
    QHash<quint64, QString> out;
    QFile f(fixture(name));
    if (!f.open(QIODevice::ReadOnly)) return out;
    DwgJson j;
    QString err;
    if (!j.parse(f.readAll(), &err)) return out;
    QHash<quint64, QString> ownedText;
    const QJsonArray objs = j.root().value("OBJECTS").toArray();
    for (const auto& v : objs) {
        const QJsonObject o = v.toObject();
        const QString t = o.value("entity").toString();
        if (t == QLatin1String("MTEXT")) ownedText.insert(DwgJson::href(o.value("ownerhandle")), o.value("text").toString());
        else if (t == QLatin1String("TEXT"))
            ownedText.insert(DwgJson::href(o.value("ownerhandle")), o.value("text_value").toString());
    }
    for (const auto& v : objs) {
        const QJsonObject o = v.toObject();
        if (o.value("entity").toString().contains(QLatin1String("DIMENSION")))
            out.insert(DwgJson::ownHandle(o), ownedText.value(DwgJson::href(o.value("block"))));
    }
    return out;
}

}  // namespace

class TestDims : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void readsLikeAutoCad();
    void styleAndOverrides();
    void arcLengthIsALength();
    void selfPasses();
    void precisionErrors_data();
    void precisionErrors();
    void precisionOff();
    void dimensionsOffByDefault();
    void relativeMode();
    void scaledDrawingSkipsPrecision();
    void formatterUnits();
    void unknownOrTypedPrecision();
    void syntheticStyleAndOverride();
    void settingsRoundTrip();

private:
    std::shared_ptr<Drawing> ref;
};

void TestDims::initTestCase() {
    ref = loadFixture("dim_ref");
    QVERIFY(ref);
    QCOMPARE(countKind(*ref, PrimKind::Dimension), 7);
}

void TestDims::readsLikeAutoCad() {
    // Every dimension in every fixture: the formatter reproduces AutoCAD's own text
    // (decimal, fractional, architectural, zero suppression, overrides, typed text,
    // R2000 / R2013 files).
    int checked = 0;
    const QStringList files = QDir(QStringLiteral(DWGCHECK_FIXTURES)).entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
    QVERIFY(!files.isEmpty());
    for (const QString& file : files) {
        const QString name = QFileInfo(file).completeBaseName();
        const QHash<quint64, QString> texts = autocadTexts(name);
        if (texts.isEmpty()) continue;
        const auto d = loadFixture(name);
        QVERIFY2(d, qPrintable(name));
        for (const auto& p : d->prims) {
            if (p.kind != PrimKind::Dimension || p.sourceId.contains(QLatin1Char('/'))) continue;
            QVERIFY2(texts.contains(p.handle), qPrintable(name + QLatin1Char(' ') + p.sourceId));
            const QString shown = dimShownText(p);
            QString expected = shown;
            if (!p.text.isEmpty() && !dimTextIsTyped(p)) expected = plainMtext(QString(p.text).replace(QLatin1String("<>"), shown));
            const QString got = plainMtext(texts.value(p.handle));
            QVERIFY2(expected == got, qPrintable(QStringLiteral("%1 %2: \"%3\", AutoCAD shows \"%4\"").arg(name, p.dimType, expected, got)));
            ++checked;
        }
    }
    QVERIFY2(checked >= 100, qPrintable(QString::number(checked)));
}

void TestDims::styleAndOverrides() {
    const Primitive* d1 = dimOf(*ref, "LINEAR", 4.0);
    QVERIFY(d1);
    QCOMPARE(d1->dimUnit, 2);
    QCOMPARE(d1->dimPrec, 3);
    QCOMPARE(dimShownText(*d1), QStringLiteral("4.000"));
    const Primitive* ang = dimOf(*ref, "ANG3PT", kPi / 2);
    QVERIFY(ang);
    QCOMPARE(ang->dimPrec, 0);
    QCOMPARE(dimShownText(*ang), QStringLiteral("90") + kDeg);
    QCOMPARE(dimShownText(*dimOf(*ref, "DIAMETER", 0.75)), QStringLiteral("Ø0.750"));

    // DIMOVERRIDE DIMDEC 2 on one dimension (stored as DSTYLE extended data).
    auto ov = loadFixture("dim_override");
    QVERIFY(ov);
    QCOMPARE(dimOf(*ov, "LINEAR", 1.5)->dimPrec, 2);
    QCOMPARE(dimOf(*ov, "LINEAR", 4.0)->dimPrec, 3);
    // Radius sub-style DC3$4: the dimension references it directly.
    auto fam = loadFixture("dim_family");
    QVERIFY(fam);
    QCOMPARE(dimOf(*fam, "RADIUS", 0.5)->dimPrec, 1);
    QCOMPARE(dimOf(*fam, "DIAMETER", 0.75)->dimPrec, 3);
    // Typed-in text replaces the measurement; "<> TYP" keeps it.
    auto tx = loadFixture("dim_text");
    QVERIFY(tx);
    const Primitive* typed = dimOf(*tx, "LINEAR", 4.0);
    QVERIFY(dimTextIsTyped(*typed));
    QCOMPARE(dimShownText(*typed), QStringLiteral("4.00"));
    const Primitive* typ = dimOf(*tx, "LINEAR", 1.5);
    QVERIFY(!dimTextIsTyped(*typ));
    QCOMPARE(dimShownText(*typ), QStringLiteral("1.500"));
    // Trailing zeros suppressed: "4" carries no decimal places.
    auto z = loadFixture("dim_zin");
    QVERIFY(z);
    QCOMPARE(dimShownText(*dimOf(*z, "LINEAR", 4.0)), QStringLiteral("4"));
    QCOMPARE(*dimPrecisionKey(*dimOf(*z, "LINEAR", 4.0)), QString());
    QCOMPARE(*dimPrecisionKey(*d1), QStringLiteral(".###"));
    QVERIFY(!dimPrecisionDiffers(*dimOf(*ref, "LINEAR", 0.625), *dimOf(*z, "LINEAR", 0.625)));  // 0.625 reads the same
}

void TestDims::arcLengthIsALength() {
    QVERIFY(!isAngularDimType("ARC_DIMENSION"));
    QVERIFY(isAngularDimType("ANG2LN") && isAngularDimType("ANG3PT"));
    const Primitive* arc = dimOf(*ref, "ARC_DIMENSION", kPi / 4);  // the R0.5 quarter arc
    QVERIFY(arc);
    QCOMPARE(dimShownText(*arc), QStringLiteral("0.785"));
    QVERIFY(std::fabs(transformed(*arc, Transform2D::scaling(2)).measurement - kPi / 2) < 1e-12);
}

void TestDims::selfPasses() {
    const CompareResult r = compareDrawings(*ref, *ref, dimSettings(), nullptr);
    QVERIFY2(r.pass, qPrintable(dumpErrors(r)));
    QCOMPARE(r.refCount, 17);  // 10 geometric primitives + 7 dimensions
}

void TestDims::precisionErrors_data() {
    QTest::addColumn<QString>("name");
    QTest::addColumn<int>("count");
    QTest::addColumn<QStringList>("messages");
    QTest::newRow("dim_prec2") << "dim_prec2" << 6
        << QStringList{QStringLiteral("linear dimension at (2.0, -0.6) reads 4.00, expected 4.000 (2 decimal places instead of 3)"),
                       QStringLiteral("linear dimension at (1.199, 2.5) reads 0.63, expected 0.625 (2 decimal places instead of 3)"),
                       QStringLiteral("diameter dimension at (2.571, 2.6) reads Ø0.75, expected Ø0.750 (2 decimal places instead of 3)"),
                       QStringLiteral("radius dimension at (5.06, 2.4) reads R0.50, expected R0.500 (2 decimal places instead of 3)"),
                       QStringLiteral("arc length dimension at (4.5, 2.5) reads 0.79, expected 0.785 (2 decimal places instead of 3)")};
    QTest::newRow("dim_angprec") << "dim_angprec" << 1
        << QStringList{QStringLiteral("angular dimension at (4.3, 2.3) reads 90.0°, expected 90° (1 decimal place instead of 0)")};
    QTest::newRow("dim_zin") << "dim_zin" << 4
        << QStringList{QStringLiteral("linear dimension at (2.0, -0.6) reads 4, expected 4.000 (trailing zeros suppressed)"),
                       QStringLiteral("linear dimension at (4.6, 0.75) reads 1.5, expected 1.500 (trailing zeros suppressed)"),
                       QStringLiteral("radius dimension at (5.06, 2.4) reads R0.5, expected R0.500 (trailing zeros suppressed)")};
    QTest::newRow("dim_override") << "dim_override" << 1
        << QStringList{QStringLiteral("linear dimension at (4.6, 0.75) reads 1.50, expected 1.500 (2 decimal places instead of 3)")};
    QTest::newRow("dim_text") << "dim_text" << 1
        << QStringList{QStringLiteral("linear dimension at (2.0, -0.6) reads 4.00, expected 4.000 (typed-in text)")};
    QTest::newRow("dim_frac") << "dim_frac" << 6
        << QStringList{QStringLiteral("linear dimension at (4.6, 0.75) reads 1 1/2, expected 1.500 (fractional units instead of decimal)"),
                       QStringLiteral("linear dimension at (1.199, 2.5) reads 5/8, expected 0.625 (fractional units instead of decimal)"),
                       QStringLiteral("arc length dimension at (4.5, 2.5) reads 13/16, expected 0.785 (fractional units instead of decimal)")};
    QTest::newRow("dim_arch") << "dim_arch" << 6
        << QStringList{QStringLiteral("linear dimension at (2.0, -0.6) reads 4\", expected 4.000 (architectural units instead of decimal)"),
                       QStringLiteral("diameter dimension at (2.571, 2.6) reads Ø3/4\", expected Ø0.750 (architectural units instead of decimal)")};
    QTest::newRow("dim_family") << "dim_family" << 1
        << QStringList{QStringLiteral("radius dimension at (5.06, 2.4) reads R0.5, expected R0.500 (1 decimal place instead of 3)")};
}

void TestDims::precisionErrors() {
    QFETCH(QString, name);
    QFETCH(int, count);
    QFETCH(QStringList, messages);
    auto d = loadFixture(name);
    QVERIFY(d);
    const CompareResult r = compareDrawings(*ref, *d, dimSettings(), nullptr);
    QVERIFY(!r.pass);
    QCOMPARE(r.refCount, 17);
    QCOMPARE(r.matched, 17 - count);  // a dimension with the wrong precision is not a match
    QCOMPARE(countCat(r, ErrCat::Precision), count);
    QVERIFY2(countFailing(r) == count, qPrintable(dumpErrors(r)));
    for (const QString& m : messages) QVERIFY2(r.errorStrings().contains(m), qPrintable(m + QStringLiteral("\n  got: ") + dumpErrors(r)));
    for (const auto& e : r.errors)
        if (e.cat == ErrCat::Precision) QVERIFY(e.refPrims.size() == 1 && e.stuPrims.size() == 1);
    QVERIFY2(r.errorSummary().startsWith(QStringLiteral("%1 wrong precision").arg(count)), qPrintable(r.errorSummary()));
}

void TestDims::precisionOff() {
    CompareSettings s = dimSettings();
    s.checkDimPrecision = false;
    for (const char* name : {"dim_prec2", "dim_angprec", "dim_zin", "dim_override", "dim_text", "dim_frac", "dim_arch", "dim_family"}) {
        auto d = loadFixture(name);
        QVERIFY(d);
        const CompareResult r = compareDrawings(*ref, *d, s, nullptr);
        QVERIFY2(r.pass, qPrintable(QStringLiteral("%1: %2").arg(QString::fromLatin1(name), dumpErrors(r))));
    }
}

void TestDims::dimensionsOffByDefault() {
    auto d = loadFixture("dim_frac");
    QVERIFY(d);
    CompareSettings s;
    s.mode = Mode::Absolute;
    QVERIFY(s.checkDimPrecision && !s.checkDimensions);
    const CompareResult r = compareDrawings(*ref, *d, s, nullptr);
    QVERIFY2(r.pass, qPrintable(dumpErrors(r)));
    QCOMPARE(r.refCount, 10);
}

void TestDims::relativeMode() {
    auto d = loadFixture("dim_override");
    QVERIFY(d);
    const auto moved = transformedCopy(*d, Transform2D::rigid(deg(37), 5, 3));
    const CompareResult r = compareDrawings(*ref, *moved, dimSettings(Mode::Relative), nullptr);
    QCOMPARE(r.matched, 16);
    QCOMPARE(countCat(r, ErrCat::Precision), 1);
    QVERIFY2(r.errorStrings().contains(QStringLiteral("linear dimension at (4.6, 0.75) reads 1.50, expected 1.500 (2 decimal places instead of 3)")),
             qPrintable(dumpErrors(r)));
}

void TestDims::scaledDrawingSkipsPrecision() {
    // Drawn twice too large with two decimal places: one scale error; the precision is
    // not judged in other units.
    auto d = loadFixture("dim_prec2");
    QVERIFY(d);
    const auto big = transformedCopy(*d, Transform2D::scaling(2));
    const CompareResult r = compareDrawings(*ref, *big, dimSettings(), nullptr);
    QVERIFY(r.scaled);
    QVERIFY(std::fabs(r.scaleFactor - 2) < 1e-9);
    QCOMPARE(r.matched, 17);
    QCOMPARE(countCat(r, ErrCat::Precision), 0);
    QVERIFY(!r.pass);
}

void TestDims::formatterUnits() {
    // Architectural and engineering feet-inches, with DIMZIN 0-3.
    QCOMPARE(dimValueText(synth("LINEAR", 54.5, 4, 4)), QStringLiteral("4'-6 1/2\""));
    QCOMPARE(dimValueText(synth("LINEAR", 48, 4, 4)), QStringLiteral("4'"));
    QCOMPARE(dimValueText(synth("LINEAR", 48, 4, 4, 3)), QStringLiteral("4'-0\""));
    QCOMPARE(dimValueText(synth("LINEAR", 6, 4, 4, 1)), QStringLiteral("0'-6\""));
    QCOMPARE(dimValueText(synth("LINEAR", 54.5, 3, 2)), QStringLiteral("4'-6.50\""));
    QVERIFY(dimPrecisionDiffers(synth("LINEAR", 54.4375, 4, 4), synth("LINEAR", 54.4375, 4, 3)));  // 6 7/16 vs 6 1/2
    QVERIFY(!dimPrecisionDiffers(synth("LINEAR", 54.5, 4, 4), synth("LINEAR", 54.5, 4, 3)));       // both 6 1/2
    // Scientific and fractional.
    QCOMPARE(dimValueText(synth("LINEAR", 4, 1, 3)), QStringLiteral("4.000E+00"));
    QCOMPARE(dimValueText(synth("LINEAR", 2.99999, 5, 4)), QStringLiteral("3"));
    // Leading zeros and the decimal separator do not change the precision.
    const Primitive lead = synth("LINEAR", 0.625, 2, 3, 4);
    QCOMPARE(dimValueText(lead), QStringLiteral(".625"));
    QVERIFY(!dimPrecisionDiffers(lead, synth("LINEAR", 0.625, 2, 3)));
    Primitive comma = synth("LINEAR", 4, 2, 3);
    comma.dimSep = QLatin1Char(',');
    QCOMPARE(dimValueText(comma), QStringLiteral("4,000"));
    QVERIFY(!dimPrecisionDiffers(comma, synth("LINEAR", 4, 2, 3)));
    // DIMRND and DIMLFAC.
    Primitive rnd = synth("LINEAR", 1.37, 2, 2);
    rnd.dimRnd = 0.25;
    QCOMPARE(dimValueText(rnd), QStringLiteral("1.25"));
    Primitive lfac = synth("LINEAR", 4, 2, 3);
    lfac.dimLfac = 2;
    QCOMPARE(dimValueText(lfac), QStringLiteral("8.000"));
    // Angles: degrees/minutes, gradians, radians, trailing zeros suppressed (DIMAZIN 2).
    QCOMPARE(dimValueText(synth("ANG2LN", deg(45.6), 1, 2)), QStringLiteral("45") + kDeg + QStringLiteral("36'"));
    QCOMPARE(dimValueText(synth("ANG2LN", deg(45.6), 1, 0)), QStringLiteral("46") + kDeg);
    QVERIFY(dimPrecisionDiffers(synth("ANG2LN", deg(45.6), 1, 2), synth("ANG2LN", deg(45.6), 1, 0)));
    QCOMPARE(dimValueText(synth("ANG2LN", kPi / 2, 2, 1)), QStringLiteral("100.0g"));
    QCOMPARE(dimValueText(synth("ANG2LN", kPi / 2, 3, 4)), QStringLiteral("1.5708r"));
    QCOMPARE(dimValueText(synth("ANG2LN", kPi / 2, 0, 2, 2)), QStringLiteral("90") + kDeg);
    // Halves round up, as in AutoCAD.
    QCOMPARE(dimValueText(synth("LINEAR", 0.625, 2, 2)), QStringLiteral("0.63"));
}

void TestDims::unknownOrTypedPrecision() {
    // No style information (e.g. synthetic or foreign files): not checked.
    const Primitive u = synth("LINEAR", 4, -1, 0);
    QVERIFY(!dimPrecisionKey(u));
    QVERIFY(dimValueText(u).isEmpty());
    QVERIFY(!dimPrecisionDiffers(u, synth("LINEAR", 4, 2, 3)));
    // Typed-in text without a number: nothing to compare.
    Primitive word = synth("LINEAR", 4, 2, 3);
    word.text = QStringLiteral("TYP");
    QVERIFY(!dimPrecisionKey(word));
    // Typed-in text: the number closest to the measured value decides.
    Primitive callout = synth("DIAMETER", 0.75, 2, 3);
    callout.text = QStringLiteral("2X %%c.750 THRU");
    QCOMPARE(*dimPrecisionKey(callout), QStringLiteral(".###"));
    QVERIFY(!dimPrecisionDiffers(synth("DIAMETER", 0.75, 2, 3), callout));
    QCOMPARE(dimShownText(callout), QStringLiteral("2X Ø.750 THRU"));
    QCOMPARE(dimPrecisionReason(synth("DIAMETER", 0.75, 2, 4), callout), QStringLiteral("typed-in text"));
}

void TestDims::syntheticStyleAndOverride() {
    DocBuilder b;
    const quint64 st = b.dimstyle("DC3", {{"DIMDEC", 3}, {"DIMADEC", -1}, {"DIMZIN", 0}, {"DIMLUNIT", 2}, {"DIMAUNIT", 0},
                                          {"DIMDSEP", 46}, {"DIMLFAC", 1.0}, {"DIMRND", 0.0}});
    const QJsonArray eed{QJsonObject{{"size", 25}, {"handle", href(0x12)}, {"code", 0}, {"value", "DSTYLE"}},
                         QJsonObject{{"code", 2}, {"value", 0}},
                         QJsonObject{{"code", 70}, {"value", 271}}, QJsonObject{{"code", 70}, {"value", 2}},
                         QJsonObject{{"code", 70}, {"value", 78}}, QJsonObject{{"code", 70}, {"value", 8}},
                         QJsonObject{{"code", 2}, {"value", 1}},
                         // another application's data, ignored
                         QJsonObject{{"size", 30}, {"handle", href(0x99)}, {"code", 70}, {"value", 271}},
                         QJsonObject{{"code", 70}, {"value", 5}}};
    b.dimension("LINEAR", 4.25, {0, 0}, {4.25, 0}, "0", {{"dimstyle", href(st)}, {"eed", eed}});
    b.dimension("LINEAR", 2.0, {0, 1}, {2, 1}, "0", {{"dimstyle", href(st)}});
    b.dimension("ANG2LN", kPi / 4, {0, 2}, {1, 2}, "0", {{"dimstyle", href(st)}});
    b.dimension("LINEAR", 3.0, {0, 3}, {3, 3});  // no style: the header's current settings
    b.header().insert("DIMDEC", 1);
    const Drawing d = b.canon();
    const Primitive* over = dimOf(d, "LINEAR", 4.25);
    QVERIFY(over);
    QCOMPARE(over->dimPrec, 2);
    QCOMPARE(over->dimZin, 8);
    QCOMPARE(dimShownText(*over), QStringLiteral("4.25"));
    QCOMPARE(dimShownText(*dimOf(d, "LINEAR", 2.0)), QStringLiteral("2.000"));
    QCOMPARE(dimShownText(*dimOf(d, "ANG2LN", kPi / 4)), QStringLiteral("45.000") + kDeg);  // DIMADEC -1: DIMDEC
    QCOMPARE(dimShownText(*dimOf(d, "LINEAR", 3.0)), QStringLiteral("3.0"));
}

void TestDims::settingsRoundTrip() {
    AppSettings a;
    QVERIFY(a.compare.checkDimPrecision);
    a.compare.checkDimPrecision = false;
    QVERIFY(!AppSettings::fromJson(a.toJson()).compare.checkDimPrecision);
    QVERIFY(AppSettings::fromJson(QJsonObject{}).compare.checkDimPrecision);  // missing key: on
}

QTEST_GUILESS_MAIN(TestDims)
#include "tst_dims.moc"
