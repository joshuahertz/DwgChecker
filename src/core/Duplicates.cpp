#include "Duplicates.h"

#include "Dsu.h"

#include <QDateTime>
#include <QHash>
#include <QMap>
#include <QSet>
#include <QTimeZone>

#include <algorithm>
#include <cstring>
#include <numeric>
#include <optional>

namespace dc {

QString strengthName(Strength s) {
    switch (s) {
    case Strength::Info: return QStringLiteral("info");
    case Strength::Weak: return QStringLiteral("weak");
    case Strength::Medium: return QStringLiteral("medium");
    case Strength::Strong: return QStringLiteral("strong");
    }
    return {};
}

QString PairResult::strongestLabel() const {
    const Signal* best = nullptr;
    for (const auto& s : evidence)
        if (!best || s.weight > best->weight) best = &s;
    return best ? best->label : QString();
}

namespace {

void appendBits(QByteArray& k, double v) {
    if (v == 0) v = 0;  // fold -0.0 into +0.0
    char buf[sizeof(double)];
    std::memcpy(buf, &v, sizeof v);
    k.append(buf, sizeof buf);
}

bool isRound(double v) {
    const double s = v * 1000.0;
    return std::fabs(s - std::round(s)) <= 1e-6 * std::max(1.0, std::fabs(s) * 1e-9);
}

// Exact key at full double precision with T = identity (geometry only).
QByteArray exactKey(const Primitive& p, bool* nontrivial) {
    QByteArray k;
    k.append(static_cast<char>(p.kind));
    switch (p.kind) {
    case PrimKind::Line: {
        Vec2 a = p.p0, b = p.p1;
        if (b.x < a.x || (b.x == a.x && b.y < a.y)) std::swap(a, b);
        for (double v : {a.x, a.y, b.x, b.y}) appendBits(k, v);
        *nontrivial = !(isRound(a.x) && isRound(a.y) && isRound(b.x) && isRound(b.y));
        break;
    }
    case PrimKind::Circle:
        for (double v : {p.center.x, p.center.y, p.radius}) appendBits(k, v);
        *nontrivial = !(isRound(p.center.x) && isRound(p.center.y) && isRound(p.radius));
        break;
    case PrimKind::Arc:
        for (double v : {p.center.x, p.center.y, p.radius, p.a0, p.a1}) appendBits(k, v);
        *nontrivial = !(isRound(p.center.x) && isRound(p.center.y) && isRound(p.radius));
        break;
    default: return {};
    }
    return k;
}

QString fmtDuration(double sec) {
    const int s = static_cast<int>(sec + 0.5);
    return QStringLiteral("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QChar('0'));
}

// Which solution drawing a pair's files are for, as the results call them: "Drawing 2", or
// "Drawings 1 and 2" for files of different parts (D82).
QString drawingsOf(const DupEntry& x, const DupEntry& y) {
    const int lo = std::min(x.group, y.group), hi = std::max(x.group, y.group);
    if (hi < 0) return {};
    if (lo < 0 || lo == hi) return QStringLiteral("Drawing %1").arg(hi + 1);
    return QStringLiteral("Drawings %1 and %2").arg(lo + 1).arg(hi + 1);
}

// When a file was created, and last saved: universal time (TDUCREATE, TDUUPDATE), else the
// computer's own (TDCREATE, TDUPDATE). The one place that chooses (D88).
const DwgTime& creationTime(const DrawingMeta& m) { return m.tducreate.valid ? m.tducreate : m.tdcreate; }
const DwgTime& saveTime(const DrawingMeta& m) { return m.tduupdate.valid ? m.tduupdate : m.tdupdate; }
QDateTime createdAt(const DrawingMeta& m) { return creationTime(m).valid ? creationTime(m).toDateTime() : QDateTime(); }
QDateTime savedAt(const DrawingMeta& m) { return saveTime(m).valid ? saveTime(m).toDateTime() : QDateTime(); }

constexpr int kOldFileMonths = 2;  // a file created this much before the class's work is old (D86)

QString timeKey(const DrawingMeta& m) {
    const DwgTime& t = creationTime(m);
    return t.valid ? QStringLiteral("%1/%2").arg(t.day).arg(t.ms) : QString();
}

// Each drawing object's exact key (empty for annotations), built once per drawing rather than for
// every pair of drawings (D76).
struct ExactKeys {
    std::vector<QByteArray> key;
    std::vector<char> nontrivial;
};

ExactKeys exactKeys(const Drawing& d) {
    ExactKeys k;
    k.key.resize(d.prims.size());
    k.nontrivial.assign(d.prims.size(), 0);
    for (size_t i = 0; i < d.prims.size(); ++i) {
        if (!isGeometric(d.prims[i].kind)) continue;
        bool nt = false;
        k.key[i] = exactKey(d.prims[i], &nt);
        k.nontrivial[i] = nt;
    }
    return k;
}

BitExactStats bitExactKeys(const ExactKeys& a, const ExactKeys& b, const std::vector<int>* onlyA, const std::vector<int>* onlyB) {
    BitExactStats st;
    QHash<QByteArray, int> counts;
    auto each = [](const ExactKeys& k, const std::vector<int>* only, auto&& f) {
        if (only) {
            for (int i : *only) f(i);
        } else {
            for (int i = 0; i < static_cast<int>(k.key.size()); ++i) f(i);
        }
    };
    int na = 0, nb = 0;
    each(a, onlyA, [&](int i) {
        if (a.key[i].isEmpty()) return;
        counts[a.key[i]]++;
        ++na;
    });
    each(b, onlyB, [&](int i) {
        if (b.key[i].isEmpty()) return;
        ++nb;
        auto it = counts.find(b.key[i]);
        if (it != counts.end() && *it > 0) {
            --*it;
            ++st.exact;
            if (b.nontrivial[i]) ++st.nontrivial;
        }
    });
    st.smaller = std::min(na, nb);
    return st;
}

}  // namespace

BitExactStats bitExact(const Drawing& a, const Drawing& b, const std::vector<int>* onlyA, const std::vector<int>* onlyB) {
    return bitExactKeys(exactKeys(a), exactKeys(b), onlyA, onlyB);
}

DupReport detectDuplicates(const std::vector<DupEntry>& E, const DuplicateSettings& s, Mode mode, const ViewSet* views,
                           const std::vector<const ViewSet*>& viewsByGroup, const std::vector<Mode>& modeByGroup) {
    DupReport rep;
    const int n = static_cast<int>(E.size());
    rep.fileNotes.resize(n);

    // Class-wide frequencies, to recognize template / shared-account values. Counted in
    // students, not files: one student's several files share a lot with each other.
    using Owners = QHash<QString, QSet<QString>>;
    // nameCount: account names, in LastSavedBy or Author alike (D77).
    Owners fpCount, timeCount, nameCount, origCount;
    QSet<QString> owners, templateFp, templateTimes;
    for (int i = 0; i < n; ++i) {
        const DupEntry& e = E[i];
        if (!e.drawing) continue;
        const DrawingMeta& m = e.drawing->meta;
        if (e.role == EntryRole::Template) {
            if (!m.fingerprintGuid.isEmpty()) templateFp.insert(m.fingerprintGuid);
            if (!timeKey(m).isEmpty()) templateTimes.insert(timeKey(m));
            continue;
        }
        if (e.role != EntryRole::Student) continue;
        const QString who = e.owner.isEmpty() ? QStringLiteral("#%1").arg(i) : e.owner;
        owners.insert(who);
        if (!m.fingerprintGuid.isEmpty()) fpCount[m.fingerprintGuid].insert(who);
        if (!timeKey(m).isEmpty()) timeCount[timeKey(m)].insert(who);
        for (const QString& name : {m.lastSavedBy, m.author})
            if (!name.isEmpty()) nameCount[name.toLower()].insert(who);
        if (!e.identity.orig.isEmpty()) origCount[e.identity.orig].insert(who);  // exact case (D83)
    }
    const int students = static_cast<int>(owners.size());
    const double limit = std::max(2.0, s.templateShareFraction * students);
    auto common = [&](const Owners& h, const QString& key) { return h.value(key).size() > limit; };
    for (auto it = fpCount.begin(); it != fpCount.end(); ++it)
        if (it.value().size() > limit && !templateFp.contains(it.key()))
            rep.notes << QStringLiteral("Probable template: FINGERPRINTGUID %1 is shared by %2 of %3 students; "
                                        "pairs sharing only this fingerprint are not flagged")
                             .arg(it.key()).arg(it.value().size()).arg(students);
    for (auto it = nameCount.begin(); it != nameCount.end(); ++it)
        if (it.value().size() > limit)
            rep.notes << QStringLiteral("Account name \"%1\" (LastSavedBy or Author) is shared by %2 of %3 students (shared lab "
                                        "account or template?); not flagged")
                             .arg(it.key()).arg(it.value().size()).arg(students);

    // Reference layer names: layers beyond these are "non-required".
    QSet<QString> refLayers{QStringLiteral("0"), QStringLiteral("DEFPOINTS")};
    for (const auto& e : E)
        if (e.role == EntryRole::Reference && e.drawing)
            for (const auto& l : e.drawing->layers) refLayers.insert(l.name.toUpper());

    // Exact keys per file, built when first needed.
    std::vector<std::optional<ExactKeys>> keys(n);
    auto keysOf = [&](int i) -> const ExactKeys& {
        if (!keys[i]) keys[i] = exactKeys(*E[i].drawing);
        return *keys[i];
    };

    for (int a = 0; a < n; ++a) {
        for (int b = a + 1; b < n; ++b) {
            const DupEntry& A = E[a];
            const DupEntry& B = E[b];
            if (!A.drawing || !B.drawing) continue;
            if (A.role != EntryRole::Student && B.role != EntryRole::Student) continue;
            // One student's own files (several parts of one assignment) are not compared.
            if (A.role == EntryRole::Student && B.role == EntryRole::Student && !A.owner.isEmpty() && A.owner == B.owner) continue;
            // Geometry only within one part of the assignment (templates go with every part).
            const bool samePart = A.group == B.group || A.role == EntryRole::Template || B.role == EntryRole::Template;
            const int part = A.role == EntryRole::Template ? B.group : A.group;
            const ViewSet* partViews =
                part >= 0 && part < static_cast<int>(viewsByGroup.size()) && viewsByGroup[part] ? viewsByGroup[part] : views;
            const Mode partMode = part >= 0 && part < static_cast<int>(modeByGroup.size()) ? modeByGroup[part] : mode;
            const DrawingMeta& ma = A.drawing->meta;
            const DrawingMeta& mb = B.drawing->meta;
            const bool withRef = A.role == EntryRole::Reference || B.role == EntryRole::Reference;
            const bool withTemplate = A.role == EntryRole::Template || B.role == EntryRole::Template;
            const bool bothStudents = A.role == EntryRole::Student && B.role == EntryRole::Student;
            PairResult pr;
            pr.a = a;
            pr.b = b;
            auto add = [&](const QString& id, const QString& label, const QString& value, const QString& detail, Strength st,
                           double w) { pr.evidence.push_back({id, label, value, detail, st, w}); };
            const QString drawings = drawingsOf(A, B);

            // 1. Byte-identical.
            if (!ma.sha256.isEmpty() && ma.sha256 == mb.sha256)
                add("sha256", QStringLiteral("byte-identical file"), drawings,
                    QStringLiteral("identical SHA-256 %1").arg(QString(ma.sha256.left(16))), Strength::Strong, 100);
            // 2. Same saved version.
            if (!ma.versionGuid.isEmpty() && ma.versionGuid == mb.versionGuid)
                add("versionguid", QStringLiteral("same VERSIONGUID"), drawings,
                    QStringLiteral("identical VERSIONGUID %1: the same saved version of one file").arg(ma.versionGuid),
                    Strength::Strong, 90);
            // 3. Same original file.
            if (!ma.fingerprintGuid.isEmpty() && ma.fingerprintGuid == mb.fingerprintGuid) {
                if (withTemplate)
                    add("fingerprint", QStringLiteral("derived from template"), drawings,
                        QStringLiteral("FINGERPRINTGUID %1 matches the template file").arg(ma.fingerprintGuid), Strength::Info, 0);
                else if (templateFp.contains(ma.fingerprintGuid) || (bothStudents && common(fpCount, ma.fingerprintGuid)))
                    ;  // whitelisted / probable template: shown in notes, not flagged
                else
                    add("fingerprint", withRef ? QStringLiteral("same fingerprint as the reference") : QStringLiteral("same fingerprint"),
                        drawings, QStringLiteral("identical FINGERPRINTGUID %1: derived from the same original file").arg(ma.fingerprintGuid),
                        Strength::Strong, 70);
            }
            // The account names the two files share (step 5), found first: step 4 needs them.
            // Which fields of a file carry a name: "LastSavedBy", "Author" or both.
            auto fieldsWith = [](const DrawingMeta& m, const QString& name) {
                QStringList f;
                if (m.lastSavedBy.compare(name, Qt::CaseInsensitive) == 0) f << QStringLiteral("LastSavedBy");
                if (m.author.compare(name, Qt::CaseInsensitive) == 0) f << QStringLiteral("Author");
                return f.join(QStringLiteral(" and "));
            };
            QStringList shared, names;
            if (!withTemplate) {
                QStringList seen;
                for (const QString& name : {ma.lastSavedBy, ma.author}) {
                    if (name.isEmpty() || seen.contains(name.toLower()) || common(nameCount, name.toLower())) continue;
                    seen << name.toLower();
                    const QString inB = fieldsWith(mb, name);
                    if (!inB.isEmpty()) {
                        shared << QStringLiteral("\"%1\" (%2's %3, %4's %5)").arg(name, A.display, fieldsWith(ma, name), B.display, inB);
                        names << QStringLiteral("\"%1\"").arg(name);
                    }
                }
            }
            // 4. Same creation time, only for files of the same drawing, or with an account name
            //    in common (user rule, D85): files for different drawings may well be started at
            //    the same second, in a lab, by students who are not working together.
            const QString ta = timeKey(ma), tb = timeKey(mb);
            const bool sameDrawing = A.group >= 0 && A.group == B.group;
            if (!ta.isEmpty() && ta == tb && !withTemplate && !templateTimes.contains(ta) &&
                !(bothStudents && common(timeCount, ta)) && (sameDrawing || !names.isEmpty())) {
                const DwgTime& t = creationTime(ma);
                const bool utc = &t == &ma.tducreate;
                add("tdcreate", QStringLiteral("same creation time"),
                    t.toDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")) + (utc ? QStringLiteral(" UTC") : QString()),
                    QStringLiteral("identical %1 %2%3 (stored to the second by AutoCAD)")
                        .arg(utc ? QStringLiteral("TDUCREATE") : QStringLiteral("TDCREATE"), t.toString(),
                             utc ? QStringLiteral(" UTC") : QString()),
                    Strength::Strong, 60);
            }
            // 5. The same account name. Each student's files carry their own, so a name in one
            //    file's LastSavedBy or Author that is also in the other's LastSavedBy or Author is
            //    strong evidence (user rules, D77), one signal for the pair; a name shared by much
            //    of the class (a lab account) is not. With the reference it stays medium: the
            //    instructor's account may come with a handed-out file. With a template, expected.
            if (!shared.isEmpty())  // none with a template
                add("accountname", QStringLiteral("same account name"), names.join(QStringLiteral(", ")),
                    QStringLiteral("both files carry the account name %1").arg(shared.join(QStringLiteral("; "))),
                    bothStudents ? Strength::Strong : Strength::Medium, bothStudents ? 60 : 30);
            // 6. Bit-exact absolute geometry. Never in Absolute mode: the placement is given there,
            //    so every correct drawing has the reference's exact coordinates (user decision).
            if (partMode == Mode::Absolute || !samePart) {
                // no signal
            } else if (partMode == Mode::Orthographic && partViews && !partViews->views.empty() && bothStudents && !A.primView.empty() &&
                       !B.primView.empty()) {
                int identical = 0, compared = 0, nontrivial = 0;
                for (int v = 0; v < static_cast<int>(partViews->views.size()); ++v) {
                    if (partViews->isIgnored(v)) continue;  // a title block handed out to everyone is not evidence
                    std::vector<int> ia, ib;
                    for (int i = 0; i < static_cast<int>(A.primView.size()); ++i) if (A.primView[i] == v) ia.push_back(i);
                    for (int i = 0; i < static_cast<int>(B.primView.size()); ++i) if (B.primView[i] == v) ib.push_back(i);
                    const BitExactStats st = bitExactKeys(keysOf(a), keysOf(b), &ia, &ib);
                    if (st.smaller < 3) continue;
                    ++compared;
                    if (st.exact >= 0.9 * st.smaller) { ++identical; nontrivial += st.nontrivial; }
                }
                if (identical > 0) {
                    const bool all = identical == compared && compared >= 2;
                    const Strength st = all ? (nontrivial >= 3 ? Strength::Strong : Strength::Medium) : Strength::Weak;
                    add("bitexact", QStringLiteral("bit-identical views"),
                        (drawings.isEmpty() ? QString() : drawings + QStringLiteral(": ")) +
                            QStringLiteral("%1 / %2").arg(identical).arg(compared),
                        QStringLiteral("%1 of %2 views have bit-identical coordinates (view spacing is free, so independent "
                                       "drawings rarely share placement)%3")
                            .arg(identical).arg(compared).arg(nontrivial < 3 ? QStringLiteral("; coordinates are round numbers") : QString()),
                        st, st == Strength::Strong ? 60 : st == Strength::Medium ? 35 : 15);
                }
            } else {
                const BitExactStats st = bitExactKeys(keysOf(a), keysOf(b), nullptr, nullptr);
                if (st.exact >= 3 && st.smaller > 0) {
                    const double frac = static_cast<double>(st.exact) / st.smaller;
                    Strength str = Strength::Info;
                    if (frac >= 0.5 && st.nontrivial >= 3) str = Strength::Strong;
                    else if (frac >= 0.8 && st.exact >= 5) str = Strength::Medium;
                    const double w = str == Strength::Strong ? 60 : str == Strength::Medium ? 35 : 5;
                    add("bitexact", QStringLiteral("bit-identical drawing objects"),
                        (drawings.isEmpty() ? QString() : drawings + QStringLiteral(": ")) +
                            QStringLiteral("%1 / %2").arg(st.exact).arg(st.smaller),
                        QStringLiteral("%1 of %2 drawing objects match at full double precision without moving the drawing (%3 "
                                       "with non-round coordinates)")
                            .arg(st.exact).arg(st.smaller).arg(st.nontrivial),
                        str, w);
                }
            }
            // 7. Informational.
            if (A.drawing->entityHandles.size() >= 3 && A.drawing->entityHandles == B.drawing->entityHandles &&
                A.drawing->entityTypes == B.drawing->entityTypes)
                add("handles", QStringLiteral("same handle sequence"), drawings,
                    QStringLiteral("identical entity creation order and handles (%1 entities)").arg(A.drawing->entityHandles.size()),
                    Strength::Info, 5);
            {
                QStringList la, lb, layerNames;
                for (const auto& l : A.drawing->layers)
                    if (!refLayers.contains(l.name.toUpper())) {
                        la << QStringLiteral("%1/%2/%3").arg(l.name.toUpper()).arg(l.rgb, 6, 16, QLatin1Char('0')).arg(l.ltype.toUpper());
                        layerNames << l.name;
                    }
                for (const auto& l : B.drawing->layers)
                    if (!refLayers.contains(l.name.toUpper())) lb << QStringLiteral("%1/%2/%3").arg(l.name.toUpper()).arg(l.rgb, 6, 16, QLatin1Char('0')).arg(l.ltype.toUpper());
                la.sort(); lb.sort();
                layerNames.sort(Qt::CaseInsensitive);
                if (!la.isEmpty() && la == lb && bothStudents)
                    add("layers", QStringLiteral("same extra layers"), layerNames.join(QStringLiteral(", ")),
                        QStringLiteral("identical non-required layers (name/color/linetype): %1").arg(la.join(", ")), Strength::Info, 5);
            }
            // The name as uploaded, capitals and all: a copied file keeps its name exactly, so names
            // that differ only in case were typed separately (user decision, D83).
            if (bothStudents && !A.identity.orig.isEmpty() && A.identity.orig == B.identity.orig && !common(origCount, A.identity.orig))
                add("origname", QStringLiteral("same original file name"), QStringLiteral("\"%1\"").arg(A.identity.orig),
                    QStringLiteral("both uploaded a file named \"%1\"").arg(A.identity.orig), Strength::Info, 5);

            if (pr.evidence.empty()) continue;
            double score = 0;
            Strength lvl = Strength::Info;
            for (const auto& sg : pr.evidence) {
                score += sg.weight;
                if (sg.strength > lvl) lvl = sg.strength;
            }
            pr.score = std::min(100.0, score);
            pr.level = lvl;
            std::stable_sort(pr.evidence.begin(), pr.evidence.end(), [](const Signal& x, const Signal& y) { return x.weight > y.weight; });
            rep.pairs.push_back(std::move(pr));
        }
    }
    std::stable_sort(rep.pairs.begin(), rep.pairs.end(), [](const PairResult& x, const PairResult& y) {
        if (x.level != y.level) return x.level > y.level;
        return x.score > y.score;
    });

    // Per-file notes.
    for (int i = 0; i < n; ++i) {
        const DupEntry& e = E[i];
        if (e.role != EntryRole::Student || !e.drawing) continue;
        const double t = e.drawing->meta.tdindwgSeconds;
        if (t >= 0 && t < s.smallEditMinutes * 60 && e.correctness >= 50)
            rep.fileNotes[i] << QStringLiteral("total editing time (TDINDWG) only %1 for a drawing that is %2% correct")
                                    .arg(fmtDuration(t)).arg(e.correctness, 0, 'f', 0);
    }

    // Several account names in one student's files (user rule, D77): a student's files should all
    // carry the same LastSavedBy, and the same Author; another name in one of them suggests a file
    // made or saved by someone else. A name shared by much of the class (a lab account, a
    // template's author) does not count. Medium: the student's files are flagged amber.
    rep.fileSignals.resize(n);
    QMap<QString, std::vector<int>> filesOf;  // by owner, in a stable order
    for (int i = 0; i < n; ++i)
        if (E[i].role == EntryRole::Student && E[i].drawing && !E[i].owner.isEmpty()) filesOf[E[i].owner].push_back(i);
    struct Field {
        const char* id;
        const char* label;
        QString DrawingMeta::*value;
    };
    const Field fields[] = {{"lastsavedby", "LastSavedBy", &DrawingMeta::lastSavedBy}, {"author", "Author", &DrawingMeta::author}};
    for (auto it = filesOf.cbegin(); it != filesOf.cend(); ++it) {
        const std::vector<int>& files = it.value();
        if (files.size() < 2) continue;
        for (const Field& f : fields) {
            QMap<QString, QStringList> byName;  // lower-case name -> the files that carry it
            QHash<QString, QString> asWritten;
            for (int i : files) {
                const QString v = E[i].drawing->meta.*f.value;
                if (v.isEmpty() || common(nameCount, v.toLower())) continue;
                byName[v.toLower()] << E[i].identity.fileName;
                asWritten.insert(v.toLower(), v);
            }
            if (byName.size() < 2) continue;
            QStringList names, quoted;
            for (auto nm = byName.cbegin(); nm != byName.cend(); ++nm) {
                names << QStringLiteral("\"%1\" in %2").arg(asWritten.value(nm.key()), nm.value().join(QStringLiteral(", ")));
                quoted << QStringLiteral("\"%1\"").arg(asWritten.value(nm.key()));
            }
            const QString who = E[files.front()].display;
            const QString detail = QStringLiteral("%1's files have different %2 names: %3").arg(who, QLatin1String(f.label),
                                                                                              names.join(QStringLiteral("; ")));
            for (int i : files)
                rep.fileSignals[i].push_back({QStringLiteral("names-") + QLatin1String(f.id),
                                              QStringLiteral("different %1 names in this student's files").arg(QLatin1String(f.label)),
                                              quoted.join(QStringLiteral(", ")), detail, Strength::Medium, 30});
        }
    }

    // A file created long before the class did the work: perhaps a file from an earlier
    // semester (user rule, D86). The class's work is the middle of its files' last saves, not
    // today, so grading them again later changes nothing, and a file handed in unchanged since
    // then is found as well. A creation time that a template has, that much of the class shares
    // (a starting file handed out), or that a solution drawing has (a pair signal says so) is
    // not this. Strong (red), as strong as a shared account name (user decision).
    std::vector<qint64> saves;
    for (const DupEntry& e : E)
        if (e.role == EntryRole::Student && e.drawing && savedAt(e.drawing->meta).isValid())
            saves.push_back(savedAt(e.drawing->meta).toMSecsSinceEpoch());
    if (!saves.empty()) {
        std::nth_element(saves.begin(), saves.begin() + saves.size() / 2, saves.end());
        const QDateTime work = QDateTime::fromMSecsSinceEpoch(saves[saves.size() / 2], QTimeZone::UTC);
        const QDateTime oldBefore = work.addMonths(-kOldFileMonths);
        QSet<QString> solutionTimes;
        for (const DupEntry& e : E)
            if (e.role == EntryRole::Reference && e.drawing && !timeKey(e.drawing->meta).isEmpty())
                solutionTimes.insert(timeKey(e.drawing->meta));
        for (int i = 0; i < n; ++i) {
            const DupEntry& e = E[i];
            if (e.role != EntryRole::Student || !e.drawing) continue;
            const DrawingMeta& m = e.drawing->meta;
            const QDateTime made = createdAt(m);
            const QString key = timeKey(m);
            if (!made.isValid() || made >= oldBefore || templateTimes.contains(key) || solutionTimes.contains(key) ||
                common(timeCount, key))
                continue;
            const QString day = made.date().toString(QStringLiteral("yyyy-MM-dd"));
            rep.fileSignals[i].push_back(
                {QStringLiteral("oldfile"), QStringLiteral("old file, created more than %1 months before the class's work").arg(kOldFileMonths),
                 day,
                 QStringLiteral("%1 was created %2, more than %3 months before the class's files were last saved (%4 in the "
                                "middle of them): perhaps a file from an earlier semester")
                     .arg(e.identity.fileName.isEmpty() ? e.display : e.identity.fileName, day, QString::number(kOldFileMonths),
                          work.date().toString(QStringLiteral("yyyy-MM-dd"))),
                 Strength::Strong, 60});
        }
    }
    return rep;
}

QString signalText(const QString& label, const QStringList& inParens) {
    return inParens.isEmpty() ? label : QStringLiteral("%1 (%2)").arg(label, inParens.join(QStringLiteral("; ")));
}

QString evidenceFileName(const DupEntry& e) {
    const QString name = e.identity.fileName.isEmpty() ? e.display : e.identity.fileName;
    switch (e.role) {
    case EntryRole::Reference: return QStringLiteral("%1 (solution drawing)").arg(name);
    case EntryRole::Template: return QStringLiteral("%1 (template)").arg(name);
    case EntryRole::Student: break;
    }
    return name;
}

QString evidenceFileLabel(const DupEntry& e) {
    const QString drawing = e.group >= 0 ? QStringLiteral("Drawing %1").arg(e.group + 1) : QString();
    switch (e.role) {
    case EntryRole::Reference: return QStringLiteral("Solution: %1").arg(drawing.isEmpty() ? e.identity.fileName : drawing);
    case EntryRole::Template: return QStringLiteral("Template: %1").arg(e.identity.fileName.isEmpty() ? e.display : e.identity.fileName);
    case EntryRole::Student: break;
    }
    return QStringLiteral("%1: %2").arg(e.identity.name.isEmpty() ? e.display : e.identity.name,
                                        drawing.isEmpty() ? QStringLiteral("extra file") : drawing);
}

std::vector<EvidenceGroup> groupEvidence(const DupReport& rep, const std::vector<DupEntry>& E, bool withInfo) {
    const int n = static_cast<int>(E.size());
    const Strength lowest = withInfo ? Strength::Info : Strength::Weak;
    // Students by owner: one student's files are one student. A file without one stands alone.
    auto ownerOf = [&](int i) { return E[i].owner.isEmpty() ? QStringLiteral("#%1").arg(i) : E[i].owner; };
    QHash<QString, QString> nameOf;
    for (int i = 0; i < n; ++i)
        if (E[i].role == EntryRole::Student) nameOf.insert(ownerOf(i), E[i].identity.name.isEmpty() ? E[i].display : E[i].identity.name);
    auto isStudent = [&](int i) { return E[i].role == EntryRole::Student; };

    // The links: all the pairs between two students, or between a student and the solution
    // drawings and templates, at the strongest pair's level.
    struct Link {
        QString a, b;  // owners (b empty: with a solution drawing or template)
        std::vector<int> pairs;
        Strength level = Strength::Info;
    };
    QMap<QString, Link> links;  // by owners, in a stable order
    for (int k = 0; k < static_cast<int>(rep.pairs.size()); ++k) {
        const PairResult& p = rep.pairs[k];
        if (!isStudent(p.a) && !isStudent(p.b)) continue;
        QString x = ownerOf(isStudent(p.a) ? p.a : p.b), y = isStudent(p.a) && isStudent(p.b) ? ownerOf(p.b) : QString();
        if (!y.isEmpty() && y < x) std::swap(x, y);
        Link& l = links[x + QChar(0x1F) + y];
        l.a = x;
        l.b = y;
        l.pairs.push_back(k);
        l.level = std::max(l.level, p.level);
    }

    std::vector<EvidenceGroup> out;
    std::vector<QSet<QString>> members;  // per group, its students (owners)
    auto append = [](EvidenceGroup& g, const std::vector<int>& ks) { g.pairs.insert(g.pairs.end(), ks.begin(), ks.end()); };

    // Students linked to each other, strongest level first. A link inside a group already made
    // (at a stronger level) adds its evidence there; the others of this level join their
    // students into new groups of this level.
    QHash<QString, int> index;
    for (const Link& l : links)
        if (!l.b.isEmpty())
            for (const QString& o : {l.a, l.b})
                if (!index.contains(o)) index.insert(o, static_cast<int>(index.size()));
    for (const Strength level : {Strength::Strong, Strength::Medium, Strength::Weak, Strength::Info}) {
        Dsu dsu(static_cast<int>(index.size()));
        std::vector<const Link*> fresh;
        const int before = static_cast<int>(out.size());
        for (const Link& l : links) {
            if (l.b.isEmpty() || l.level != level) continue;
            int inside = -1;
            for (int g = 0; g < before && inside < 0; ++g)
                if (members[g].contains(l.a) && members[g].contains(l.b)) inside = g;
            if (inside >= 0) {
                append(out[inside], l.pairs);
            } else if (level >= lowest) {
                dsu.unite(index.value(l.a), index.value(l.b));
                fresh.push_back(&l);
            }
        }
        QHash<int, int> groupOfRoot;
        for (const Link* l : fresh) {
            const int root = dsu.find(index.value(l->a));
            auto it = groupOfRoot.find(root);
            if (it == groupOfRoot.end()) {
                out.emplace_back();
                out.back().level = level;
                members.emplace_back();
                it = groupOfRoot.insert(root, static_cast<int>(out.size()) - 1);
            }
            append(out[*it], l->pairs);
            members[*it] << l->a << l->b;
        }
    }
    // A student and the solution drawings or templates.
    for (const Link& l : links)
        if (l.b.isEmpty() && l.level >= lowest) {
            EvidenceGroup g;
            g.level = l.level;
            g.pairs = l.pairs;
            out.push_back(std::move(g));
            members.push_back({l.a});
        }
    // A student whose own files disagree: each kind of signal once.
    QMap<QString, std::vector<int>> ownFiles;
    for (int i = 0; i < n && i < static_cast<int>(rep.fileSignals.size()); ++i)
        if (!rep.fileSignals[i].empty()) ownFiles[ownerOf(i)].push_back(i);
    for (auto it = ownFiles.cbegin(); it != ownFiles.cend(); ++it) {
        EvidenceGroup g;
        for (const int i : it.value()) {
            g.files.push_back(i);
            double sum = 0;
            for (const Signal& s : rep.fileSignals[i]) {
                sum += s.weight;
                g.level = std::max(g.level, s.strength);
                if (std::none_of(g.ownSignals.cbegin(), g.ownSignals.cend(),
                                 [&](const Signal& o) { return o.id == s.id && o.detail == s.detail; }))
                    g.ownSignals.push_back(s);
            }
            g.score = std::max(g.score, std::min(100.0, sum));
        }
        if (g.level < lowest) continue;
        out.push_back(std::move(g));
        members.push_back({it.key()});
    }

    // What each group shows: its students and files, its score, and its signals merged.
    for (int gi = 0; gi < static_cast<int>(out.size()); ++gi) {
        EvidenceGroup& g = out[gi];
        std::sort(g.pairs.begin(), g.pairs.end());  // the pairs are strongest first
        QSet<int> files(g.files.cbegin(), g.files.cend());
        int refs = 0, templates = 0;
        for (const int k : g.pairs) {
            const PairResult& p = rep.pairs[k];
            g.score = std::max(g.score, p.score);
            for (const int i : {p.a, p.b})
                if (!files.contains(i)) {
                    files.insert(i);
                    refs += E[i].role == EntryRole::Reference;
                    templates += E[i].role == EntryRole::Template;
                }
        }
        g.files.assign(files.cbegin(), files.cend());
        std::sort(g.files.begin(), g.files.end(), [&](int x, int y) {
            if (isStudent(x) != isStudent(y)) return isStudent(x);
            const int c = isStudent(x) ? nameOf.value(ownerOf(x)).compare(nameOf.value(ownerOf(y)), Qt::CaseInsensitive) : 0;
            if (c != 0) return c < 0;
            const int f = E[x].identity.fileName.compare(E[y].identity.fileName, Qt::CaseInsensitive);
            // Then the run's order: the files come out of a QSet, whose order changes from run
            // to run (Qt seeds its hashing at random), so equal names must not be left to it.
            return f != 0 ? f < 0 : x < y;
        });
        for (const QString& o : members[gi]) g.students << nameOf.value(o, o);
        g.students.sort(Qt::CaseInsensitive);
        const QString solution = refs > 1 ? QStringLiteral("the solution drawings") : QStringLiteral("the solution drawing");
        const QString tmpl = templates > 1 ? QStringLiteral("the templates") : QStringLiteral("the template");
        g.with = refs && templates ? solution + QStringLiteral(" and ") + tmpl : refs ? solution : templates ? tmpl : QString();

        // Each kind of signal with each value once; the students that share it, when only some
        // of a larger group do.
        struct Merged {
            QString label, value;
            Strength strength = Strength::Info;
            double weight = 0;
            QSet<QString> owners;
        };
        std::vector<Merged> merged;
        auto merge = [&](const Signal& s, const QSet<QString>& owners) {
            auto m = std::find_if(merged.begin(), merged.end(), [&](const Merged& x) { return x.label == s.label && x.value == s.value; });
            if (m == merged.end()) {
                merged.push_back({s.label, s.value, s.strength, s.weight, {}});
                m = merged.end() - 1;
            }
            m->strength = std::max(m->strength, s.strength);
            m->weight = std::max(m->weight, s.weight);
            m->owners.unite(owners);
        };
        for (const int k : g.pairs) {
            const PairResult& p = rep.pairs[k];
            QSet<QString> owners;
            for (const int i : {p.a, p.b})
                if (isStudent(i)) owners.insert(ownerOf(i));
            for (const Signal& s : p.evidence) merge(s, owners);
        }
        for (const Signal& s : g.ownSignals) merge(s, members[gi]);
        std::stable_sort(merged.begin(), merged.end(), [](const Merged& x, const Merged& y) {
            return x.weight != y.weight ? x.weight > y.weight : x.strength > y.strength;
        });
        for (const Merged& m : merged) {
            QStringList inParens;
            if (!m.value.isEmpty()) inParens << m.value;
            if (members[gi].size() > 2 && m.owners.size() < members[gi].size()) {
                QStringList who;
                for (const QString& o : m.owners) who << nameOf.value(o, o);
                who.sort(Qt::CaseInsensitive);
                inParens << who.join(QStringLiteral(", "));
            }
            g.lines.push_back({signalText(m.label, inParens), m.strength, m.weight});
        }
    }
    std::stable_sort(out.begin(), out.end(), [](const EvidenceGroup& x, const EvidenceGroup& y) {
        if (x.level != y.level) return x.level > y.level;
        if (x.score != y.score) return x.score > y.score;
        if (x.students.size() != y.students.size()) return x.students.size() > y.students.size();
        return x.students.value(0).compare(y.students.value(0), Qt::CaseInsensitive) < 0;
    });
    return out;
}

}  // namespace dc
