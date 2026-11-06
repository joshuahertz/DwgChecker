#include "Duplicates.h"

#include <QHash>
#include <QSet>

#include <algorithm>
#include <cstring>
#include <numeric>

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

struct Dsu {
    std::vector<int> p;
    explicit Dsu(int n) : p(n) { std::iota(p.begin(), p.end(), 0); }
    int find(int x) { while (p[x] != x) x = p[x] = p[p[x]]; return x; }
    void unite(int a, int b) { a = find(a); b = find(b); if (a != b) p[std::max(a, b)] = std::min(a, b); }
};

QString fmtDuration(double sec) {
    const int s = static_cast<int>(sec + 0.5);
    return QStringLiteral("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QChar('0'));
}

QString timeKey(const DrawingMeta& m) {
    const DwgTime& t = m.tducreate.valid ? m.tducreate : m.tdcreate;
    return t.valid ? QStringLiteral("%1/%2").arg(t.day).arg(t.ms) : QString();
}

}  // namespace

BitExactStats bitExact(const Drawing& a, const Drawing& b, const std::vector<int>* onlyA, const std::vector<int>* onlyB) {
    BitExactStats st;
    QHash<QByteArray, int> counts;
    QHash<QByteArray, bool> nontriv;
    auto each = [](const Drawing& d, const std::vector<int>* only, auto&& f) {
        if (only) { for (int i : *only) f(d.prims[i]); }
        else for (const auto& p : d.prims) f(p);
    };
    int na = 0, nb = 0;
    each(a, onlyA, [&](const Primitive& p) {
        if (!isGeometric(p.kind)) return;
        bool nt = false;
        const QByteArray k = exactKey(p, &nt);
        counts[k]++;
        nontriv[k] = nt;
        ++na;
    });
    each(b, onlyB, [&](const Primitive& p) {
        if (!isGeometric(p.kind)) return;
        ++nb;
        bool nt = false;
        const QByteArray k = exactKey(p, &nt);
        auto it = counts.find(k);
        if (it != counts.end() && *it > 0) {
            --*it;
            ++st.exact;
            if (nt) ++st.nontrivial;
        }
    });
    st.smaller = std::min(na, nb);
    return st;
}

DupReport detectDuplicates(const std::vector<DupEntry>& E, const DuplicateSettings& s, Mode mode, const ViewSet* views) {
    DupReport rep;
    const int n = static_cast<int>(E.size());
    rep.fileNotes.resize(n);

    // Class-wide frequencies, to recognize template / shared-account values.
    int students = 0;
    QHash<QString, int> fpCount, timeCount, savedByCount, authorCount, origCount;
    QSet<QString> templateFp, templateTimes;
    for (const auto& e : E) {
        if (!e.drawing) continue;
        const DrawingMeta& m = e.drawing->meta;
        if (e.role == EntryRole::Template) {
            if (!m.fingerprintGuid.isEmpty()) templateFp.insert(m.fingerprintGuid);
            if (!timeKey(m).isEmpty()) templateTimes.insert(timeKey(m));
            continue;
        }
        if (e.role != EntryRole::Student) continue;
        ++students;
        if (!m.fingerprintGuid.isEmpty()) fpCount[m.fingerprintGuid]++;
        if (!timeKey(m).isEmpty()) timeCount[timeKey(m)]++;
        if (!m.lastSavedBy.isEmpty()) savedByCount[m.lastSavedBy.toLower()]++;
        if (!m.author.isEmpty()) authorCount[m.author.toLower()]++;
        if (!e.identity.orig.isEmpty()) origCount[e.identity.orig.toLower()]++;
    }
    const double limit = std::max(2.0, s.templateShareFraction * students);
    auto common = [&](const QHash<QString, int>& h, const QString& key) { return h.value(key) > limit; };
    for (auto it = fpCount.begin(); it != fpCount.end(); ++it)
        if (it.value() > limit && !templateFp.contains(it.key()))
            rep.notes << QStringLiteral("Probable template: FINGERPRINTGUID %1 is shared by %2 of %3 students; "
                                        "pairs sharing only this fingerprint are not flagged")
                             .arg(it.key()).arg(it.value()).arg(students);
    for (auto it = savedByCount.begin(); it != savedByCount.end(); ++it)
        if (it.value() > limit)
            rep.notes << QStringLiteral("LastSavedBy \"%1\" is shared by %2 of %3 students (shared lab account?); not flagged")
                             .arg(it.key()).arg(it.value()).arg(students);

    // Reference layer names: layers beyond these are "non-required".
    QSet<QString> refLayers{QStringLiteral("0"), QStringLiteral("DEFPOINTS")};
    for (const auto& e : E)
        if (e.role == EntryRole::Reference && e.drawing)
            for (const auto& l : e.drawing->layers) refLayers.insert(l.name.toUpper());

    for (int a = 0; a < n; ++a) {
        for (int b = a + 1; b < n; ++b) {
            const DupEntry& A = E[a];
            const DupEntry& B = E[b];
            if (!A.drawing || !B.drawing) continue;
            if (A.role != EntryRole::Student && B.role != EntryRole::Student) continue;
            const DrawingMeta& ma = A.drawing->meta;
            const DrawingMeta& mb = B.drawing->meta;
            const bool withRef = A.role == EntryRole::Reference || B.role == EntryRole::Reference;
            const bool withTemplate = A.role == EntryRole::Template || B.role == EntryRole::Template;
            const bool bothStudents = A.role == EntryRole::Student && B.role == EntryRole::Student;
            PairResult pr;
            pr.a = a;
            pr.b = b;
            auto add = [&](const QString& id, const QString& label, const QString& detail, Strength st, double w) {
                pr.evidence.push_back({id, label, detail, st, w});
            };

            // 1. Byte-identical.
            if (!ma.sha256.isEmpty() && ma.sha256 == mb.sha256)
                add("sha256", QStringLiteral("byte-identical file"), QStringLiteral("identical SHA-256 %1").arg(QString(ma.sha256.left(16))),
                    Strength::Strong, 100);
            // 2. Same saved version.
            if (!ma.versionGuid.isEmpty() && ma.versionGuid == mb.versionGuid)
                add("versionguid", QStringLiteral("same VERSIONGUID"),
                    QStringLiteral("identical VERSIONGUID %1: the same saved version of one file").arg(ma.versionGuid),
                    Strength::Strong, 90);
            // 3. Same original file.
            if (!ma.fingerprintGuid.isEmpty() && ma.fingerprintGuid == mb.fingerprintGuid) {
                if (withTemplate)
                    add("fingerprint", QStringLiteral("derived from template"),
                        QStringLiteral("FINGERPRINTGUID %1 matches the template file").arg(ma.fingerprintGuid), Strength::Info, 0);
                else if (templateFp.contains(ma.fingerprintGuid) || (bothStudents && common(fpCount, ma.fingerprintGuid)))
                    ;  // whitelisted / probable template: shown in notes, not flagged
                else
                    add("fingerprint", withRef ? QStringLiteral("same fingerprint as the reference") : QStringLiteral("same fingerprint"),
                        QStringLiteral("identical FINGERPRINTGUID %1: derived from the same original file").arg(ma.fingerprintGuid),
                        Strength::Strong, 70);
            }
            // 4. Same creation time.
            const QString ta = timeKey(ma), tb = timeKey(mb);
            if (!ta.isEmpty() && ta == tb && !withTemplate && !templateTimes.contains(ta) &&
                !(bothStudents && common(timeCount, ta))) {
                const DwgTime& t = ma.tducreate.valid ? ma.tducreate : ma.tdcreate;
                add("tdcreate", QStringLiteral("same creation time"),
                    QStringLiteral("identical TDUCREATE %1 UTC (stored to the second by AutoCAD)").arg(t.toString()),
                    Strength::Strong, 60);
            }
            // 5. Shared identity fields.
            if (!ma.lastSavedBy.isEmpty() && ma.lastSavedBy.compare(mb.lastSavedBy, Qt::CaseInsensitive) == 0 &&
                !common(savedByCount, ma.lastSavedBy.toLower()))
                add("lastsavedby", QStringLiteral("same LastSavedBy"),
                    QStringLiteral("both files were last saved by \"%1\"").arg(ma.lastSavedBy), Strength::Medium, 30);
            if (!ma.author.isEmpty() && ma.author.compare(mb.author, Qt::CaseInsensitive) == 0 &&
                !common(authorCount, ma.author.toLower()))
                add("author", QStringLiteral("same Author"), QStringLiteral("both files have Author \"%1\"").arg(ma.author),
                    Strength::Medium, 30);
            if (bothStudents) {
                auto otherName = [&](const DupEntry& X, const DupEntry& Y) {
                    for (const QString& who : {X.drawing->meta.lastSavedBy, X.drawing->meta.author}) {
                        if (who.isEmpty()) continue;
                        if (personMatchesStudent(who, Y.identity.name) && !personMatchesStudent(who, X.identity.name))
                            add("savedby-other", QStringLiteral("saved by %1").arg(Y.display),
                                QStringLiteral("%1's file names \"%2\" as LastSavedBy/Author, which matches student %3")
                                    .arg(X.display, who, Y.display),
                                Strength::Strong, 50);
                    }
                };
                otherName(A, B);
                otherName(B, A);
            }
            // 6. Bit-exact absolute geometry.
            if (mode == Mode::Orthographic && views && !views->views.empty() && bothStudents && !A.primView.empty() &&
                !B.primView.empty()) {
                int identical = 0, compared = 0, nontrivial = 0;
                for (int v = 0; v < static_cast<int>(views->views.size()); ++v) {
                    std::vector<int> ia, ib;
                    for (int i = 0; i < static_cast<int>(A.primView.size()); ++i) if (A.primView[i] == v) ia.push_back(i);
                    for (int i = 0; i < static_cast<int>(B.primView.size()); ++i) if (B.primView[i] == v) ib.push_back(i);
                    const BitExactStats st = bitExact(*A.drawing, *B.drawing, &ia, &ib);
                    if (st.smaller < 3) continue;
                    ++compared;
                    if (st.exact >= 0.9 * st.smaller) { ++identical; nontrivial += st.nontrivial; }
                }
                if (identical > 0) {
                    const bool all = identical == compared && compared >= 2;
                    const Strength st = all ? (nontrivial >= 3 ? Strength::Strong : Strength::Medium) : Strength::Weak;
                    add("bitexact", QStringLiteral("%1 of %2 views bit-identical").arg(identical).arg(compared),
                        QStringLiteral("%1 of %2 views have bit-identical coordinates (view spacing is free, so independent "
                                       "drawings rarely share placement)%3")
                            .arg(identical).arg(compared).arg(nontrivial < 3 ? QStringLiteral("; coordinates are round numbers") : QString()),
                        st, st == Strength::Strong ? 60 : st == Strength::Medium ? 35 : 15);
                }
            } else {
                const BitExactStats st = bitExact(*A.drawing, *B.drawing);
                if (st.exact >= 3 && st.smaller > 0) {
                    const double frac = static_cast<double>(st.exact) / st.smaller;
                    Strength str = Strength::Info;
                    if (frac >= 0.5 && st.nontrivial >= 3) str = Strength::Strong;
                    else if (frac >= 0.8 && st.exact >= 5) str = Strength::Medium;
                    if (mode == Mode::Absolute) str = st.nontrivial >= 3 && frac >= 0.5 ? Strength::Medium : Strength::Info;
                    const double w = str == Strength::Strong ? 60 : str == Strength::Medium ? 35 : 5;
                    add("bitexact", QStringLiteral("%1 of %2 primitives bit-identical").arg(st.exact).arg(st.smaller),
                        QStringLiteral("%1 of %2 primitives match at full double precision without moving the drawing (%3 with "
                                       "non-round coordinates)%4")
                            .arg(st.exact).arg(st.smaller).arg(st.nontrivial)
                            .arg(mode == Mode::Absolute ? QStringLiteral("; weak evidence when placement is specified") : QString()),
                        str, w);
                }
            }
            // 7. Informational.
            if (A.drawing->entityHandles.size() >= 3 && A.drawing->entityHandles == B.drawing->entityHandles &&
                A.drawing->entityTypes == B.drawing->entityTypes)
                add("handles", QStringLiteral("same handle sequence"),
                    QStringLiteral("identical entity creation order and handles (%1 entities)").arg(A.drawing->entityHandles.size()),
                    Strength::Info, 5);
            {
                QStringList la, lb;
                for (const auto& l : A.drawing->layers)
                    if (!refLayers.contains(l.name.toUpper())) la << QStringLiteral("%1/%2/%3").arg(l.name.toUpper()).arg(l.color).arg(l.ltype.toUpper());
                for (const auto& l : B.drawing->layers)
                    if (!refLayers.contains(l.name.toUpper())) lb << QStringLiteral("%1/%2/%3").arg(l.name.toUpper()).arg(l.color).arg(l.ltype.toUpper());
                la.sort(); lb.sort();
                if (!la.isEmpty() && la == lb && bothStudents)
                    add("layers", QStringLiteral("same extra layers"),
                        QStringLiteral("identical non-required layers (name/color/linetype): %1").arg(la.join(", ")), Strength::Info, 5);
            }
            if (bothStudents && !A.identity.orig.isEmpty() && A.identity.orig.compare(B.identity.orig, Qt::CaseInsensitive) == 0 &&
                !common(origCount, A.identity.orig.toLower()))
                add("origname", QStringLiteral("same original file name"),
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

    // Clusters joined by strong evidence.
    Dsu dsu(n);
    for (const auto& p : rep.pairs)
        if (p.level == Strength::Strong) dsu.unite(p.a, p.b);
    QHash<int, std::vector<int>> comps;
    for (int i = 0; i < n; ++i) comps[dsu.find(i)].push_back(i);
    for (auto& c : comps)
        if (c.size() >= 2) rep.clusters.push_back(c);
    std::sort(rep.clusters.begin(), rep.clusters.end(), [](const auto& x, const auto& y) {
        if (x.size() != y.size()) return x.size() > y.size();
        return x.front() < y.front();
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
    return rep;
}

}  // namespace dc
