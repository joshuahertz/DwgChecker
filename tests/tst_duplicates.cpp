// Duplicate / collaboration detection (M5).

#include "Duplicates.h"
#include "TestUtil.h"

#include <QRegularExpression>
#include <QtTest>

#include <algorithm>

using namespace dc;
using namespace dct;

namespace {

DupEntry student(const QString& name, DrawingPtr d, double correctness = 100) {
    DupEntry e;
    e.role = EntryRole::Student;
    e.display = name;
    e.identity.name = name;
    e.identity.parsed = true;
    e.drawing = d;
    e.correctness = correctness;
    return e;
}
DupEntry reference(DrawingPtr d) {
    DupEntry e;
    e.role = EntryRole::Reference;
    e.display = "reference";
    e.drawing = d;
    return e;
}
const PairResult* pairOf(const DupReport& r, int a, int b) {
    for (const auto& p : r.pairs)
        if ((p.a == a && p.b == b) || (p.a == b && p.b == a)) return &p;
    return nullptr;
}
bool has(const PairResult* p, const QString& id) {
    if (!p) return false;
    for (const auto& s : p->evidence)
        if (s.id == id) return true;
    return false;
}
const Signal* signalOf(const PairResult* p, const QString& id) {
    if (!p) return nullptr;
    for (const auto& s : p->evidence)
        if (s.id == id) return &s;
    return nullptr;
}
QStringList linesOf(const EvidenceGroup& g) {
    QStringList out;
    for (const auto& l : g.lines) out << l.text;
    return out;
}
// A synthetic drawing with chosen metadata and a few drawing objects.
std::shared_ptr<Drawing> synth(const QString& fp, const QString& savedBy, double offset, int handlesFrom = 0x264) {
    auto d = std::make_shared<Drawing>();
    d->meta.fingerprintGuid = fp;
    d->meta.versionGuid = QStringLiteral("{V-%1-%2}").arg(fp).arg(offset);
    d->meta.sha256 = QStringLiteral("sha-%1-%2").arg(fp).arg(offset).toLatin1();
    d->meta.lastSavedBy = savedBy;
    for (int i = 0; i < 6; ++i) {
        Primitive p;
        p.kind = PrimKind::Line;
        p.p0 = {offset + i, offset};
        p.p1 = {offset + i, offset + 1.5};
        d->prims.push_back(p);
        d->entityHandles.push_back(handlesFrom + i);
        d->entityTypes << "LINE";
    }
    return d;
}

}  // namespace

class TestDuplicates : public QObject {
    Q_OBJECT
private slots:
    void corpusSignals();
    void templateFingerprintWhitelisted();
    void sharedFingerprintIsProbableTemplate();
    void sameAccountNameIsStrong();
    void severalNamesInOneStudentsFiles();
    void bitExactNonRoundIsStrong();
    void absoluteModeIgnoresBitExact();
    void groupsOfStudents();
    void weakerLinkGetsItsOwnRow();
    void groupNamesWhoSharesASignal();
    void studentWithTheSolutionDrawing();
    void originalNameMatchesCase();
    void creationTimeNeedsSameDrawingOrName();
    void oldFileFromAnEarlierSemester();
    void smallEditingTimeNote();
};

void TestDuplicates::corpusSignals() {
    auto ref = loadFixture("ref");
    auto byteCopy = loadFixture("ref_bytecopy");
    auto saveAs = loadFixture("ref_saveas");
    auto copyPaste = loadFixture("ref_copypaste");
    auto redraw = loadFixture("ref_redraw");
    auto translated = loadFixture("ref_translated");
    QVERIFY(ref && byteCopy && saveAs && copyPaste && redraw && translated);
    std::vector<DupEntry> e{student("bytecopy", byteCopy), student("saveas", saveAs), student("copypaste", copyPaste),
                            student("redraw", redraw), student("translated", translated), reference(ref)};
    const DupReport r = detectDuplicates(e, DuplicateSettings{}, Mode::Relative, nullptr);

    const PairResult* bc = pairOf(r, 0, 5);
    QVERIFY(has(bc, "sha256"));
    QVERIFY(has(bc, "versionguid"));
    QCOMPARE(bc->level, Strength::Strong);

    const PairResult* sa = pairOf(r, 1, 5);
    QVERIFY(has(sa, "fingerprint"));
    QVERIFY(has(sa, "tdcreate"));
    // The table shows the creation time itself (D82).
    static const QRegularExpression when(QStringLiteral(R"(^\d{4}-\d\d-\d\d \d\d:\d\d:\d\d( UTC)?$)"));
    QVERIFY2(when.match(signalOf(sa, "tdcreate")->value).hasMatch(), qPrintable(signalOf(sa, "tdcreate")->value));
    QVERIFY(!has(sa, "sha256"));
    QCOMPARE(sa->level, Strength::Strong);

    const PairResult* cp = pairOf(r, 2, 5);
    QVERIFY(cp);
    QVERIFY(!has(cp, "fingerprint"));  // new drawing, new fingerprint
    QVERIFY(has(cp, "bitexact"));      // but bit-identical coordinates
    QVERIFY(cp->level >= Strength::Medium);

    // Independent redraw and a translated drawing: no copy evidence against the reference.
    const PairResult* rd = pairOf(r, 3, 5);
    QVERIFY(!rd || rd->level < Strength::Weak);
    const PairResult* tr = pairOf(r, 4, 5);
    QVERIFY(!has(tr, "bitexact") || tr->level < Strength::Weak);

    // Everyone was saved by the same login, which is shared by most of the class: not flagged.
    for (const auto& p : r.pairs) QVERIFY(!has(&p, "lastsavedby"));
}

void TestDuplicates::templateFingerprintWhitelisted() {
    auto tpl = synth("{TPL}", "", 100);
    auto a = synth("{TPL}", "", 0);
    auto b = synth("{TPL}", "", 50);
    DupEntry t;
    t.role = EntryRole::Template;
    t.display = "template";
    t.drawing = tpl;
    std::vector<DupEntry> e{student("a", a), student("b", b), student("c", synth("{C}", "", 7)), student("d", synth("{D}", "", 9)),
                            student("e", synth("{E}", "", 11)), student("f", synth("{F}", "", 13)), student("g", synth("{G}", "", 15)), t};
    const DupReport r = detectDuplicates(e, DuplicateSettings{}, Mode::Relative, nullptr);
    QVERIFY(!has(pairOf(r, 0, 1), "fingerprint"));
    const PairResult* at = pairOf(r, 0, 7);
    QVERIFY(at);
    QVERIFY(has(at, "fingerprint"));
    QCOMPARE(at->level, Strength::Info);  // derived from the template: informational only
}

void TestDuplicates::sharedFingerprintIsProbableTemplate() {
    std::vector<DupEntry> e;
    for (int i = 0; i < 6; ++i) e.push_back(student(QStringLiteral("s%1").arg(i), synth("{SHARED}", "", i * 10.0 + 0.123)));
    for (int i = 6; i < 10; ++i) e.push_back(student(QStringLiteral("s%1").arg(i), synth(QStringLiteral("{U%1}").arg(i), "", i * 10.0)));
    const DupReport r = detectDuplicates(e, DuplicateSettings{}, Mode::Relative, nullptr);
    QVERIFY(!has(pairOf(r, 0, 1), "fingerprint"));
    bool noted = false;
    for (const QString& n : r.notes) noted = noted || n.contains("Probable template");
    QVERIFY(noted);
}

void TestDuplicates::sameAccountNameIsStrong() {
    // Each student's files carry their own account name: two students with the same one is
    // strong evidence, whatever the name looks like (D77).
    std::vector<DupEntry> e{student("smithjane", synth("{A}", "x4471", 0)), student("doejohn", synth("{B}", "x4471", 20)),
                            student("x", synth("{X}", "x", 40)), student("y", synth("{Y}", "y", 60))};
    const DupReport r = detectDuplicates(e, DuplicateSettings{}, Mode::Relative, nullptr);
    const PairResult* p = pairOf(r, 0, 1);
    QVERIFY(has(p, "accountname"));
    QCOMPARE(p->level, Strength::Strong);
    QVERIFY(!pairOf(r, 0, 2) || !has(pairOf(r, 0, 2), "accountname"));

    // Across the two fields: one file's LastSavedBy is the other's Author.
    auto withAuthor = [](std::shared_ptr<Drawing> d, const QString& author) {
        d->meta.author = author;
        return d;
    };
    std::vector<DupEntry> x{student("smithjane", withAuthor(synth("{A}", "jsmith", 0), "jsmith")),
                            student("doejohn", withAuthor(synth("{B}", "jdoe", 20), "jsmith")),
                            student("x", synth("{X}", "x", 40)), student("y", synth("{Y}", "y", 60))};
    const DupReport rx = detectDuplicates(x, DuplicateSettings{}, Mode::Relative, nullptr);
    const PairResult* px = pairOf(rx, 0, 1);
    QVERIFY(has(px, "accountname"));
    QCOMPARE(px->level, Strength::Strong);
    // One signal for the pair, the name listed once with where each file has it.
    const auto named = std::count_if(px->evidence.begin(), px->evidence.end(), [](const Signal& s) { return s.id == "accountname"; });
    QCOMPARE(static_cast<int>(named), 1);
    for (const Signal& s : px->evidence)
        if (s.id == "accountname")
            QVERIFY2(s.detail.contains(QStringLiteral("smithjane's LastSavedBy and Author, doejohn's Author")), qPrintable(s.detail));
}

void TestDuplicates::severalNamesInOneStudentsFiles() {
    // One student's two files saved under two account names: both files get the signal, at
    // medium (amber); a student whose files agree gets none (D77).
    std::vector<DupEntry> e{student("smithjane", synth("{A}", "jsmith", 0)), student("smithjane", synth("{A2}", "jdoe", 10)),
                            student("x", synth("{X}", "x", 40)), student("x", synth("{X2}", "x", 50)),
                            student("y", synth("{Y}", "y", 60))};
    e[0].owner = e[1].owner = QStringLiteral("smithjane");
    e[2].owner = e[3].owner = QStringLiteral("x");
    e[4].owner = QStringLiteral("y");
    const DupReport r = detectDuplicates(e, DuplicateSettings{}, Mode::Relative, nullptr);
    QCOMPARE(r.fileSignals.size(), e.size());
    for (int i : {0, 1}) {
        QCOMPARE(r.fileSignals[i].size(), size_t(1));
        QCOMPARE(r.fileSignals[i][0].id, QStringLiteral("names-lastsavedby"));
        QCOMPARE(r.fileSignals[i][0].strength, Strength::Medium);
        QVERIFY(r.fileSignals[i][0].detail.contains(QStringLiteral("\"jdoe\"")));
    }
    for (int i : {2, 3, 4}) QVERIFY(r.fileSignals[i].empty());
    // A row of its own in the table, the names in parentheses (D82), and not a note as well.
    const std::vector<EvidenceGroup> g = groupEvidence(r, e, false);
    QCOMPARE(g.size(), size_t(1));
    QCOMPARE(g[0].students, QStringList{QStringLiteral("smithjane")});
    QCOMPARE(g[0].level, Strength::Medium);
    QCOMPARE(g[0].files, (std::vector<int>{0, 1}));
    QCOMPARE(linesOf(g[0]), QStringList{QStringLiteral("different LastSavedBy names in this student's files (\"jdoe\", \"jsmith\")")});
    for (const QString& n : r.notes) QVERIFY(!n.contains(QStringLiteral("different LastSavedBy names")));
}

void TestDuplicates::bitExactNonRoundIsStrong() {
    std::vector<DupEntry> e{student("a", synth("{A}", "", 13.48291034)), student("b", synth("{B}", "", 13.48291034)),
                            student("c", synth("{C}", "", 3.0))};
    const DupReport r = detectDuplicates(e, DuplicateSettings{}, Mode::Relative, nullptr);
    const PairResult* ab = pairOf(r, 0, 1);
    QVERIFY(has(ab, "bitexact"));
    QCOMPARE(ab->level, Strength::Strong);
    QVERIFY(!has(pairOf(r, 0, 2), "bitexact"));
    QCOMPARE(bitExact(*e[0].drawing, *e[1].drawing).exact, 6);
    QCOMPARE(bitExact(*e[0].drawing, *e[1].drawing).nontrivial, 6);
}

void TestDuplicates::absoluteModeIgnoresBitExact() {
    // Placement is given in Absolute mode, so identical coordinates are expected: no signal
    // at all, even for non-round coordinates that would be strong evidence in Relative mode.
    std::vector<DupEntry> e{student("a", synth("{A}", "", 2.0)), student("b", synth("{B}", "", 2.0)),
                            student("c", synth("{C}", "", 13.48291034)), student("d", synth("{D}", "", 13.48291034))};
    const DupReport abs = detectDuplicates(e, DuplicateSettings{}, Mode::Absolute, nullptr);
    for (const auto& p : abs.pairs) QVERIFY(!has(&p, "bitexact"));
    const DupReport rel = detectDuplicates(e, DuplicateSettings{}, Mode::Relative, nullptr);
    QVERIFY(has(pairOf(rel, 2, 3), "bitexact"));
}

// The Possible cheating table: a row per group of students linked by evidence (D82).
void TestDuplicates::groupsOfStudents() {
    auto shared = synth("{S}", "", 1.111);
    std::vector<DupEntry> e{student("a", shared), student("b", shared), student("c", synth("{S}", "", 3.333))};
    // Enough classmates that 3 sharing a fingerprint stays under the 30% "probable template" limit.
    for (int i = 0; i < 9; ++i) e.push_back(student(QStringLiteral("f%1").arg(i), synth(QStringLiteral("{F%1}").arg(i), "", 50.0 + 10 * i)));
    const DupReport r = detectDuplicates(e, DuplicateSettings{}, Mode::Relative, nullptr);
    const std::vector<EvidenceGroup> g = groupEvidence(r, e, false);
    QCOMPARE(g.size(), size_t(1));  // the classmates share only the handle sequence: informational
    QCOMPARE(g[0].students, (QStringList{"a", "b", "c"}));
    QCOMPARE(g[0].files, (std::vector<int>{0, 1, 2}));
    QCOMPARE(g[0].level, Strength::Strong);
    QCOMPARE(g[0].score, 100.0);
    QVERIFY(g[0].with.isEmpty());
    const QStringList lines = linesOf(g[0]);
    QCOMPARE(lines.value(0), QStringLiteral("byte-identical file (Drawing 1; a, b)"));  // only a and b
    QVERIFY2(lines.contains(QStringLiteral("same fingerprint (Drawing 1)")), qPrintable(lines.join(" | ")));  // all three
    QCOMPARE(lines.count(QStringLiteral("same fingerprint (Drawing 1)")), 1);  // once for its three pairs
    QCOMPARE(g[0].pairs.size(), size_t(3));
    // With informational links too, the classmates' rows follow at that level.
    const std::vector<EvidenceGroup> all = groupEvidence(r, e, true);
    QVERIFY(all.size() > 1);
    QCOMPARE(all[0].students, g[0].students);
    for (size_t k = 1; k < all.size(); ++k) QCOMPARE(all[k].level, Strength::Info);
}

void TestDuplicates::weakerLinkGetsItsOwnRow() {
    // a and b: the same account name (strong); b and c: the same round coordinates (medium).
    // c is not pulled into the strong group: the medium link is a row of its own.
    std::vector<DupEntry> e{student("a", synth("{A}", "jsmith", 40)), student("b", synth("{B}", "jsmith", 2.0)),
                            student("c", synth("{C}", "c", 2.0))};
    const DupReport r = detectDuplicates(e, DuplicateSettings{}, Mode::Relative, nullptr);
    const std::vector<EvidenceGroup> g = groupEvidence(r, e, false);
    QCOMPARE(g.size(), size_t(2));
    QCOMPARE(g[0].students, (QStringList{"a", "b"}));
    QCOMPARE(g[0].level, Strength::Strong);
    QCOMPARE(linesOf(g[0]).value(0), QStringLiteral("same account name (\"jsmith\")"));
    QCOMPARE(g[1].students, (QStringList{"b", "c"}));
    QCOMPARE(g[1].level, Strength::Medium);
    QCOMPARE(linesOf(g[1]).value(0), QStringLiteral("bit-identical drawing objects (Drawing 1: 6 / 6)"));
}

void TestDuplicates::groupNamesWhoSharesASignal() {
    // Three students from one original file; two of them also under one account name. In the
    // group's row, the signal all three share has no names; the one two share names them.
    std::vector<DupEntry> e{student("a", synth("{S}", "jsmith", 0)), student("b", synth("{S}", "jsmith", 20)),
                            student("c", synth("{S}", "c", 40))};
    for (int i = 0; i < 9; ++i) e.push_back(student(QStringLiteral("f%1").arg(i), synth(QStringLiteral("{F%1}").arg(i), "", 50.0 + 10 * i)));
    const DupReport r = detectDuplicates(e, DuplicateSettings{}, Mode::Relative, nullptr);
    const std::vector<EvidenceGroup> g = groupEvidence(r, e, false);
    QCOMPARE(g.size(), size_t(1));
    QCOMPARE(g[0].students, (QStringList{"a", "b", "c"}));
    const QStringList lines = linesOf(g[0]);
    QCOMPARE(lines.value(0), QStringLiteral("same fingerprint (Drawing 1)"));
    QCOMPARE(lines.value(1), QStringLiteral("same account name (\"jsmith\"; a, b)"));
}

void TestDuplicates::studentWithTheSolutionDrawing() {
    // A student's file from the solution drawing's original: a row for that student "with the
    // solution drawing", whose file is listed as one.
    std::vector<DupEntry> e{student("a", synth("{R}", "", 0)), student("x", synth("{X}", "", 20)), student("y", synth("{Y}", "", 40)),
                            reference(synth("{R}", "", 60))};
    const DupReport r = detectDuplicates(e, DuplicateSettings{}, Mode::Relative, nullptr);
    const std::vector<EvidenceGroup> g = groupEvidence(r, e, false);
    QCOMPARE(g.size(), size_t(1));
    QCOMPARE(g[0].students, QStringList{QStringLiteral("a")});
    QCOMPARE(g[0].with, QStringLiteral("the solution drawing"));
    QCOMPARE(g[0].files, (std::vector<int>{0, 3}));
    QCOMPARE(evidenceFileName(e[3]), QStringLiteral("reference (solution drawing)"));
    // Short, as in the results (D84).
    QCOMPARE(evidenceFileLabel(e[0]), QStringLiteral("a: Drawing 1"));
    QCOMPARE(evidenceFileLabel(e[3]), QStringLiteral("Solution: Drawing 1"));
    DupEntry extra = e[1];
    extra.group = -1;
    QCOMPARE(evidenceFileLabel(extra), QStringLiteral("x: extra file"));
    QCOMPARE(linesOf(g[0]).value(0), QStringLiteral("same fingerprint as the reference (Drawing 1)"));
}

// The uploaded file's name must match exactly, capitals included; counted that way for
// "shared by much of the class" too (user decision, D83).
void TestDuplicates::originalNameMatchesCase() {
    std::vector<DupEntry> e{student("a", synth("{A}", "", 0)), student("b", synth("{B}", "", 20)),
                            student("c", synth("{C}", "", 40)), student("x", synth("{X}", "", 60))};
    e[0].identity.orig = e[1].identity.orig = QStringLiteral("Lab3.dwg");
    e[2].identity.orig = QStringLiteral("lab3.dwg");
    e[3].identity.orig = QStringLiteral("other.dwg");
    const DupReport r = detectDuplicates(e, DuplicateSettings{}, Mode::Relative, nullptr);
    // Ignoring case, three of four students would share the name, which would make it common.
    QVERIFY(has(pairOf(r, 0, 1), "origname"));
    QCOMPARE(signalOf(pairOf(r, 0, 1), "origname")->value, QStringLiteral("\"Lab3.dwg\""));
    QVERIFY(!has(pairOf(r, 0, 2), "origname"));
    QVERIFY(!has(pairOf(r, 1, 2), "origname"));
}

// The same creation time counts for files of the same drawing, or with an account name in
// common; not for two students' files of different drawings (user rule, D85).
void TestDuplicates::creationTimeNeedsSameDrawingOrName() {
    auto make = [](const QString& fp, const QString& savedBy, double offset, int group) {
        auto d = synth(fp, savedBy, offset);
        d->meta.tducreate = DwgTime{2460000, 3600000, true};
        DupEntry e = student(fp, d);
        e.owner = fp;
        e.group = group;
        return e;
    };
    auto timeSignal = [&](const DupEntry& a, const DupEntry& b) {
        const std::vector<DupEntry> e{a, b, student("x", synth("{X}", "x", 40)), student("y", synth("{Y}", "y", 60))};
        return has(pairOf(detectDuplicates(e, DuplicateSettings{}, Mode::Relative, nullptr), 0, 1), "tdcreate");
    };
    QVERIFY(timeSignal(make("{A}", "a", 0, 1), make("{B}", "b", 20, 1)));         // the same drawing
    QVERIFY(!timeSignal(make("{A}", "a", 0, 1), make("{B}", "b", 20, 2)));        // different drawings
    QVERIFY(timeSignal(make("{A}", "jsmith", 0, 1), make("{B}", "jsmith", 20, 2)));  // ... with a name in common
    QVERIFY(!timeSignal(make("{A}", "a", 0, -1), make("{B}", "b", 20, -1)));      // extra files: no drawing
}

// A file created more than 2 months before the class's files were last saved (user rule,
// D86); measured from the class's work, not today.
void TestDuplicates::oldFileFromAnEarlierSemester() {
    const qint64 day = 2461000;  // Julian day of the class's work
    auto make = [&](const QString& name, qint64 created, qint64 saved, double offset) {
        auto d = synth(QStringLiteral("{%1}").arg(name), name, offset);
        d->meta.tducreate = DwgTime{created, 36000000, true};
        d->meta.tduupdate = DwgTime{saved, 36000000, true};
        return student(name, d);
    };
    std::vector<DupEntry> e{make("a", day - 100, day, 0), make("b", day - 30, day, 20), make("c", day - 1, day, 40),
                            make("d", day - 2, day + 1, 60)};
    const DupReport r = detectDuplicates(e, DuplicateSettings{}, Mode::Relative, nullptr);
    QCOMPARE(r.fileSignals[0].size(), size_t(1));
    QCOMPARE(r.fileSignals[0][0].id, QStringLiteral("oldfile"));
    QCOMPARE(r.fileSignals[0][0].strength, Strength::Strong);  // red (user decision)
    QCOMPARE(r.fileSignals[0][0].weight, 60.0);
    QCOMPARE(r.fileSignals[0][0].value, QDate::fromJulianDay(day - 100).toString(QStringLiteral("yyyy-MM-dd")));
    for (int i : {1, 2, 3}) QVERIFY(r.fileSignals[i].empty());  // a month old is not old
    // Its own row in the table.
    const std::vector<EvidenceGroup> g = groupEvidence(r, e, false);
    QCOMPARE(g.size(), size_t(1));
    QCOMPARE(g[0].students, QStringList{QStringLiteral("a")});
    QCOMPARE(g[0].level, Strength::Strong);
    QCOMPARE(g[0].score, 60.0);
    QCOMPARE(linesOf(g[0]).value(0), QStringLiteral("old file, created more than 2 months before the class's work (%1)")
                                         .arg(QDate::fromJulianDay(day - 100).toString(QStringLiteral("yyyy-MM-dd"))));

    // The creation time of a template, or of a starting file much of the class has: not old.
    DupEntry t;
    t.role = EntryRole::Template;
    t.display = QStringLiteral("template");
    t.drawing = e[0].drawing;
    std::vector<DupEntry> withTemplate = e;
    withTemplate.push_back(t);
    QVERIFY(detectDuplicates(withTemplate, DuplicateSettings{}, Mode::Relative, nullptr).fileSignals[0].empty());
    std::vector<DupEntry> handedOut{make("a", day - 100, day, 0), make("b", day - 100, day, 20), make("c", day - 100, day, 40),
                                    make("d", day - 2, day, 60)};
    for (const auto& s : detectDuplicates(handedOut, DuplicateSettings{}, Mode::Relative, nullptr).fileSignals) QVERIFY(s.empty());
}

void TestDuplicates::smallEditingTimeNote() {
    auto d = synth("{A}", "", 0);
    d->meta.tdindwgSeconds = 42;
    std::vector<DupEntry> e{student("a", d, 95), student("b", synth("{B}", "", 9), 95)};
    const DupReport r = detectDuplicates(e, DuplicateSettings{}, Mode::Relative, nullptr);
    QCOMPARE(r.fileNotes[0].size(), 1);
    QVERIFY(r.fileNotes[0][0].contains("0:42"));
    QVERIFY(r.fileNotes[1].isEmpty());
}

QTEST_GUILESS_MAIN(TestDuplicates)
#include "tst_duplicates.moc"
