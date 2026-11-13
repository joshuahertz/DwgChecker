#include "ClassSummary.h"

#include "DimText.h"
#include "Format.h"

#include <QHash>
#include <QSet>

#include <algorithm>

namespace dc {

static void meanMedian(const std::vector<double>& v, double* mean, double* median);

int SolutionPartCount::count(const QString& label) const {
    for (const auto& [l, n] : how)
        if (l == label) return n;
    return 0;
}

DrawingSummary summarizeDrawing(const BatchOutput& b, int ref) {
    DrawingSummary s;
    s.ref = ref;
    if (ref < 0 || ref >= static_cast<int>(b.refs.size()) || !b.refs[ref]) return s;
    const int nPrims = static_cast<int>(b.refs[ref]->prims.size());
    std::vector<SolutionPartCount> parts(nPrims);
    for (int i = 0; i < nPrims; ++i) parts[i].prim = i;
    std::vector<QHash<QString, int>> how(nPrims);
    QHash<QString, ErrorKindCount> kinds;
    QStringList kindOrder;  // first seen first: ties keep the results' order

    for (const StudentGroup& g : b.groups) {
        const int f = ref < static_cast<int>(g.byRef.size()) ? g.byRef[ref] : -1;
        if (f < 0 || !b.students[f].ok()) {
            ++s.noFile;
            continue;
        }
        const CompareResult& r = b.students[f].result;
        ++s.graded;
        s.scores.push_back(r.correctness);
        // Each kind once per file for "students", every error for "times"; each part's kinds once per file.
        QSet<QString> fileKinds;
        QSet<QPair<int, QString>> partKinds;
        int failing = 0;
        for (const ErrorItem& e : r.errors) {
            if (!e.failing || e.label.isEmpty()) continue;
            ++failing;
            ErrorKindCount& k = kinds[e.label];
            if (k.label.isEmpty()) {
                k.label = e.label;
                kindOrder << e.label;
            }
            ++k.times;
            k.onSolution = k.onSolution || !e.refPrims.empty();
            fileKinds.insert(e.label);
            for (const int i : e.refPrims)
                if (i >= 0 && i < nPrims && !partKinds.contains({i, e.label})) {
                    partKinds.insert({i, e.label});
                    ++how[i][e.label];
                }
        }
        for (const QString& l : fileKinds) ++kinds[l].students;
        if (failing > 0) ++s.withErrors;
        s.errorCount += failing;
        const int n = std::min(nPrims, static_cast<int>(r.refStatus.size()));
        for (int i = 0; i < n; ++i) {
            const PrimStatus st = r.refStatus[i];
            if (st == PrimStatus::NotCompared || st == PrimStatus::Extra) continue;
            ++parts[i].compared;
            if (st != PrimStatus::Matched) ++parts[i].missed;
        }
    }

    std::sort(s.scores.begin(), s.scores.end());
    meanMedian(s.scores, &s.mean, &s.median);
    for (const QString& l : kindOrder) s.kinds.push_back(kinds.value(l));
    std::stable_sort(s.kinds.begin(), s.kinds.end(), [](const ErrorKindCount& x, const ErrorKindCount& y) {
        return x.students != y.students ? x.students > y.students : x.times > y.times;
    });

    for (int i = 0; i < nPrims; ++i) {
        if (parts[i].compared == 0) continue;
        for (auto it = how[i].cbegin(); it != how[i].cend(); ++it) parts[i].how.push_back({it.key(), it.value()});
        std::sort(parts[i].how.begin(), parts[i].how.end(),
                  [](const auto& x, const auto& y) { return x.second != y.second ? x.second > y.second : x.first < y.first; });
        s.parts.push_back(std::move(parts[i]));
    }
    std::stable_sort(s.parts.begin(), s.parts.end(), [](const SolutionPartCount& x, const SolutionPartCount& y) {
        if (x.missed != y.missed) return x.missed > y.missed;
        return x.rate() > y.rate();
    });
    s.partOf.assign(nPrims, -1);
    for (int k = 0; k < static_cast<int>(s.parts.size()); ++k) s.partOf[s.parts[k].prim] = k;
    return s;
}

// Mean and median of sorted values.
static void meanMedian(const std::vector<double>& v, double* mean, double* median) {
    *mean = *median = 0;
    if (v.empty()) return;
    double sum = 0;
    for (const double x : v) sum += x;
    *mean = sum / static_cast<double>(v.size());
    const size_t m = v.size() / 2;
    *median = v.size() % 2 ? v[m] : (v[m - 1] + v[m]) / 2;
}

ClassOverall summarizeClass(const BatchOutput& b) {
    ClassOverall o;
    for (const StudentGroup& g : b.groups) {
        ++o.students;
        o.scores.push_back(g.correctness);
        o.complete += !g.byRef.empty() &&
                      std::all_of(g.byRef.cbegin(), g.byRef.cend(), [&](int f) { return f >= 0 && b.students[f].ok(); });
    }
    std::sort(o.scores.begin(), o.scores.end());
    meanMedian(o.scores, &o.mean, &o.median);
    return o;
}

QString describePart(const BatchOutput& b, int ref, int prim) {
    if (ref < 0 || ref >= static_cast<int>(b.refs.size()) || !b.refs[ref]) return {};
    const Drawing& d = *b.refs[ref];
    if (prim < 0 || prim >= static_cast<int>(d.prims.size())) return {};
    const Primitive& p = d.prims[prim];
    QString what;
    switch (p.kind) {
    case PrimKind::Line: what = QStringLiteral("line %1 long").arg(fmtLen(p.length())); break;
    case PrimKind::Circle: what = QStringLiteral("circle Ø%1").arg(fmtLen(2 * p.radius)); break;
    case PrimKind::Arc: what = QStringLiteral("arc R%1").arg(fmtLen(p.radius)); break;
    case PrimKind::Dimension: {
        QString t = dimShownText(p);
        if (t.isEmpty()) t = isAngularDimType(p.dimType) ? fmtDeg(p.measurement) : fmtLen(p.measurement);
        what = QStringLiteral("dimension %1").arg(t);
        break;
    }
    case PrimKind::Hatch: what = QStringLiteral("hatch %1").arg(p.text); break;
    default: what = kindName(p.kind).toLower(); break;
    }
    what += QStringLiteral(" on %1").arg(p.layer);
    if (b.hasViews && ref < static_cast<int>(b.refModes.size()) && b.refModes[ref] == Mode::Orthographic &&
        ref < static_cast<int>(b.views.size())) {
        const ViewSet& vs = b.views[ref];
        const int v = prim < static_cast<int>(vs.primView.size()) ? vs.primView[prim] : -1;
        if (v >= 0) what += QStringLiteral(" (%1)").arg(vs.label(v));
    }
    return what;
}

double commonMissRate(const std::vector<double>& rates) {
    double c = 0;
    for (const double r : rates)
        if (r <= 0.5 && r > c) c = r;
    return c > 0 ? c : 0.5;
}

double missLevel(double rate, double common) {
    if (rate <= 0) return 0;
    if (rate > 0.5) return 2;
    return common > 0 ? std::min(1.0, rate / common) : 1.0;
}

QString describeHow(const SolutionPartCount& p) {
    QStringList out;
    for (const auto& [label, n] : p.how) out << QStringLiteral("%1 %2").arg(n).arg(label);
    return out.join(QStringLiteral(", "));
}

}  // namespace dc
