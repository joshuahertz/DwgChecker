#include "Batch.h"

#include "Canonicalizer.h"

#include <QDir>
#include <QHash>
#include <QFileInfo>
#include <QMutex>
#include <QThread>
#include <QThreadPool>

#include <algorithm>
#include <limits>

namespace dc {

QStringList findSubmissionFiles(const QString& dir) {
    QDir d(dir);
    QStringList names = d.entryList({QStringLiteral("*.dwg"), QStringLiteral("*.DWG")}, QDir::Files, QDir::Name);
    if (names.isEmpty()) names = d.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
    names.removeDuplicates();
    QStringList out;
    for (const QString& n : names)
        if (!n.endsWith(QStringLiteral(".views.json"))) out << d.absoluteFilePath(n);
    return out;
}

ViewSet resolveReferenceViews(const Drawing& ref, const QString& refPath, const CompareSettings& s,
                              const std::optional<ViewSet>& explicitViews, QString* note) {
    if (explicitViews && explicitViews->refSha256 == ref.meta.sha256 && !explicitViews->views.empty()) {
        // Matched by source id, so changed canonicalization settings cannot shift assignments.
        if (note) *note = QStringLiteral("views: confirmed in the editor");
        return explicitViews->rebindTo(ref);
    }
    ViewSet vs;
    QString err;
    if (loadSidecar(sidecarPath(refPath), ref, &vs, &err)) {
        if (note) *note = QStringLiteral("views: loaded from %1").arg(QFileInfo(sidecarPath(refPath)).fileName());
        return vs;
    }
    vs = segmentViews(ref, s.orthoGapFraction);
    if (note) {
        QStringList names;
        for (int i = 0; i < static_cast<int>(vs.views.size()); ++i) names << vs.label(i);
        *note = QStringLiteral("views: auto-segmented (%1)%2").arg(names.join(", "),
                QFileInfo::exists(sidecarPath(refPath)) ? QStringLiteral("; sidecar ignored: ") + err : QString());
    }
    return vs;
}

QString studentKey(const StudentIdentity& id) { return id.parsed ? id.name + QLatin1Char('_') + id.uid : id.fileName; }

std::vector<int> assignOneToOne(const std::vector<std::vector<double>>& weight) {
    const int rows = static_cast<int>(weight.size());
    if (rows == 0) return {};
    const int cols = static_cast<int>(weight[0].size());
    std::vector<int> out(rows, -1);
    if (cols == 0) return out;
    // Hungarian method (Kuhn-Munkres) on a square cost matrix: cost = maxW - weight, and
    // padding rows/columns cost maxW (worth nothing), so every real column is used while
    // real rows last and vice versa.
    const int n = std::max(rows, cols);
    double maxW = 0;
    for (const auto& row : weight)
        for (double w : row) maxW = std::max(maxW, w);
    auto cost = [&](int i, int j) {  // 1-based
        return i <= rows && j <= cols ? maxW - weight[i - 1][j - 1] : maxW;
    };
    const double inf = std::numeric_limits<double>::infinity();
    std::vector<double> u(n + 1, 0), v(n + 1, 0);
    std::vector<int> p(n + 1, 0), way(n + 1, 0);  // p[j]: row on column j
    for (int i = 1; i <= n; ++i) {
        p[0] = i;
        int j0 = 0;
        std::vector<double> minv(n + 1, inf);
        std::vector<char> used(n + 1, 0);
        do {
            used[j0] = 1;
            const int i0 = p[j0];
            double delta = inf;
            int j1 = 0;
            for (int j = 1; j <= n; ++j) {
                if (used[j]) continue;
                const double cur = cost(i0, j) - u[i0] - v[j];
                if (cur < minv[j]) {
                    minv[j] = cur;
                    way[j] = j0;
                }
                if (minv[j] < delta) {
                    delta = minv[j];
                    j1 = j;
                }
            }
            for (int j = 0; j <= n; ++j) {
                if (used[j]) {
                    u[p[j]] += delta;
                    v[j] -= delta;
                } else {
                    minv[j] -= delta;
                }
            }
            j0 = j1;
        } while (p[j0] != 0);
        do {
            const int j1 = way[j0];
            p[j0] = p[j1];
            j0 = j1;
        } while (j0 != 0);
    }
    for (int j = 1; j <= cols; ++j)
        if (p[j] >= 1 && p[j] <= rows) out[p[j] - 1] = j - 1;
    return out;
}

namespace {

constexpr double kNameBonus = 5.0;  // percentage points for a file name that matches the solution's

// Letters and digits only, lower case: "Part A.dwg" -> "parta".
QString nameKey(const QString& fileName) {
    QString out;
    for (const QChar c : QFileInfo(fileName).completeBaseName().toLower())
        if (c.isLetterOrNumber()) out += c;
    return out;
}

bool namesMatch(const QString& a, const QString& b) {
    if (a.isEmpty() || b.isEmpty()) return false;
    if (a == b) return true;
    return (a.size() >= 3 && b.contains(a)) || (b.size() >= 3 && a.contains(b));
}

// Matches one student's readable files to the solution drawings one to one, for the best
// total fit over all of them (user decision): one drawing's best file may go elsewhere when
// that serves the whole set better, and the last file takes the last drawing. The fit is the
// correctness wherever the file was drawn, so an Absolute-mode drawing in the wrong place
// still finds its drawing; a matching file name adds kNameBonus. Only with more files than
// drawings are some left over as extras.
void assignFiles(BatchOutput& out, StudentGroup& g, std::vector<std::vector<CompareResult>>& results,
                 const QStringList& refKeys) {
    const int nRefs = static_cast<int>(out.refs.size());
    g.byRef.assign(nRefs, -1);
    std::vector<int> files;  // readable ones
    std::vector<std::vector<double>> weight;
    for (int f : g.files) {
        StudentRun& run = out.students[f];
        run.refIndex = -1;
        if (!run.ok()) continue;
        const QString key = nameKey(run.identity.parsed ? run.identity.orig : run.identity.fileName);
        double best = -1;
        std::vector<double> row;
        for (int r = 0; r < nRefs; ++r) {
            const double c = results[f][r].fit();
            if (c > best) {
                best = c;
                run.closestRef = r;
            }
            row.push_back(c + (namesMatch(key, refKeys[r]) ? kNameBonus : 0.0));
        }
        files.push_back(f);
        weight.push_back(std::move(row));
    }
    const std::vector<int> pick = assignOneToOne(weight);
    for (int k = 0; k < static_cast<int>(files.size()); ++k) {
        if (pick[k] < 0) continue;
        out.students[files[k]].refIndex = pick[k];
        g.byRef[pick[k]] = files[k];
    }
    double sum = 0;
    for (int f : g.files) {
        StudentRun& run = out.students[f];
        if (!run.ok()) continue;
        run.result = std::move(results[f][out.refOf(run)]);
        results[f].clear();
        if (run.matched()) sum += run.result.correctness;
    }
    g.correctness = nRefs > 0 ? sum / nRefs : 0.0;
}

}  // namespace

BatchOutput runBatch(const BatchInput& in, const IDrawingLoader& loader, const ProgressFn& progress,
                     const std::atomic_bool* cancel) {
    BatchOutput out;
    out.mode = in.settings.compare.mode;
    out.parser = loader.parserVersion();
    out.log << QStringLiteral("parser: %1").arg(out.parser);
    const CanonSettings canon = in.settings.effectiveCanon();
    const CompareSettings& cmp = in.settings.compare;
    if (in.refPaths.isEmpty()) {
        out.error = QStringLiteral("no solution drawing");
        return out;
    }
    const int total = static_cast<int>(in.refPaths.size() + in.studentPaths.size() + in.templatePaths.size());
    std::atomic_int done{0};
    QMutex progressMutex;
    auto report = [&](const QString& what) {
        const int d = ++done;
        if (progress) {
            QMutexLocker lock(&progressMutex);
            progress(d, total, what);
        }
    };

    // ---- The solution drawing(s), and their views in Orthographic mode ----
    const CanonSettings refCanon = in.settings.referenceCanon();  // what the instructor turned off is not required
    for (const QString& rp : in.refPaths) {
        QString err;
        auto ref = loadDrawing(rp, loader, refCanon, &err);
        report(QFileInfo(rp).fileName());
        if (!ref) {
            out.error = QStringLiteral("solution drawing %1: %2").arg(rp, err);
            return out;
        }
        out.refPaths << rp;
        out.refs.push_back(ref);
        out.log << QStringLiteral("reference: %1 (sha256 %2, %3 primitives)")
                       .arg(rp, QString(ref->meta.sha256.left(16))).arg(ref->prims.size());
        for (const auto& w : ref->warnings)
            if (w.kind == WarnKind::HiddenLayer) out.log << QStringLiteral("%1: %2").arg(QFileInfo(rp).fileName(), w.text);
    }
    const int nRefs = static_cast<int>(out.refs.size());
    // Each solution drawing's settings: its own mode and checks when there are several.
    std::vector<CompareSettings> cmpOf;
    for (int r = 0; r < nRefs; ++r) {
        cmpOf.push_back(nRefs > 1 ? in.settings.compareFor(out.refPaths[r]) : cmp);
        out.refModes.push_back(cmpOf.back().mode);
        if (nRefs > 1)
            out.log << QStringLiteral("%1: %2 mode").arg(QFileInfo(out.refPaths[r]).fileName(), modeName(cmpOf.back().mode));
    }
    std::vector<const ViewSet*> viewsPtr(nRefs, nullptr);
    out.views.resize(nRefs);
    for (int r = 0; r < nRefs; ++r) {
        if (cmpOf[r].mode != Mode::Orthographic) continue;
        out.hasViews = true;
        {
            QString note;
            const auto it = in.views.constFind(out.refPaths[r]);
            const std::optional<ViewSet> explicitViews = it != in.views.cend() ? std::optional<ViewSet>(*it) : std::nullopt;
            out.views[r] = resolveReferenceViews(*out.refs[r], out.refPaths[r], cmpOf[r], explicitViews, &note);
            out.log << (nRefs > 1 ? QFileInfo(out.refPaths[r]).fileName() + QStringLiteral(": ") + note : note);
        }
        viewsPtr[r] = &out.views[r];
    }

    // ---- Student files, read and compared with every solution drawing in parallel ----
    const IdentityParser idp(in.settings.identityRegex);
    out.students.resize(in.studentPaths.size());
    std::vector<std::vector<CompareResult>> results(in.studentPaths.size());
    QThreadPool pool;
    pool.setMaxThreadCount(std::max(1, QThread::idealThreadCount()));
    for (int i = 0; i < static_cast<int>(in.studentPaths.size()); ++i) {
        StudentRun& run = out.students[i];
        run.path = in.studentPaths[i];
        run.identity = idp.parse(run.path);
        pool.start([&, i]() {
            StudentRun& r = out.students[i];
            if (cancel && cancel->load()) {
                r.loadError = QStringLiteral("cancelled");
                report(r.identity.fileName);
                return;
            }
            try {
                QString e;
                auto d = loadDrawing(r.path, loader, canon, &e);
                if (!d) r.loadError = e;
                else {
                    std::vector<CompareResult> res;
                    res.reserve(nRefs);
                    for (int k = 0; k < nRefs; ++k) res.push_back(compareDrawings(*out.refs[k], *d, cmpOf[k], viewsPtr[k]));
                    results[i] = std::move(res);
                    r.drawing = d;
                }
            } catch (const std::exception& ex) {
                r.loadError = QStringLiteral("internal error: %1").arg(QString::fromLocal8Bit(ex.what()));
                r.drawing.reset();
            } catch (...) {
                r.loadError = QStringLiteral("internal error");
                r.drawing.reset();
            }
            report(r.identity.fileName);
        });
    }
    for (const QString& t : in.templatePaths) {
        QString e;
        auto d = loadDrawing(t, loader, canon, &e);
        if (d) out.templates.push_back(d);
        else out.log << QStringLiteral("template %1: %2").arg(t, e);
        report(QFileInfo(t).fileName());
    }
    pool.waitForDone();
    out.cancelled = cancel && cancel->load();

    // ---- One group per student; their files matched to the solution drawings ----
    QHash<QString, int> groupOf;
    for (int i = 0; i < static_cast<int>(out.students.size()); ++i) {
        const QString key = studentKey(out.students[i].identity);
        auto it = groupOf.find(key);
        if (it == groupOf.end()) {
            StudentGroup g;
            g.key = key;
            g.identity = out.students[i].identity;
            out.groups.push_back(g);
            it = groupOf.insert(key, static_cast<int>(out.groups.size()) - 1);
        }
        out.groups[*it].files.push_back(i);
    }
    std::stable_sort(out.groups.begin(), out.groups.end(), [](const StudentGroup& a, const StudentGroup& b) {
        const int c = a.identity.name.compare(b.identity.name, Qt::CaseInsensitive);
        return c != 0 ? c < 0 : a.key < b.key;
    });
    QStringList refKeys;
    for (const QString& rp : out.refPaths) refKeys << nameKey(rp);
    int extras = 0, missing = 0;
    for (int g = 0; g < static_cast<int>(out.groups.size()); ++g) {
        StudentGroup& grp = out.groups[g];
        for (int f : grp.files) out.students[f].group = g;
        assignFiles(out, grp, results, refKeys);
        for (int f : grp.files) extras += out.students[f].ok() && !out.students[f].matched();
        for (int f : grp.byRef) missing += f < 0;
    }
    if (nRefs > 1 || extras > 0)
        out.log << QStringLiteral("%1 students; %2 extra file(s) not matched to a solution drawing; %3 solution drawing(s) "
                                  "without a file")
                       .arg(out.groups.size()).arg(extras).arg(missing);

    // ---- Duplicate detection: student files, then the solution drawings, then templates ----
    for (const StudentRun& r : out.students) {
        DupEntry e;
        e.role = EntryRole::Student;
        e.display = r.identity.name;
        if (nRefs > 1 && r.ok())
            e.display += QStringLiteral(" (%1)").arg(r.matched() ? QFileInfo(out.refPaths[r.refIndex]).fileName() : r.identity.fileName);
        e.identity = r.identity;
        e.drawing = r.drawing;
        e.correctness = r.ok() && r.matched() ? r.result.correctness : -1;
        e.group = r.ok() ? r.refIndex : -1;
        e.owner = studentKey(r.identity);
        if (r.ok()) e.primView = r.result.stuView;
        out.dupEntries.push_back(e);
    }
    for (int k = 0; k < nRefs; ++k) {
        DupEntry e;
        e.role = EntryRole::Reference;
        e.display = QStringLiteral("reference (%1)").arg(QFileInfo(out.refPaths[k]).fileName());
        e.identity.fileName = QFileInfo(out.refPaths[k]).fileName();
        e.drawing = out.refs[k];
        e.group = k;
        out.dupEntries.push_back(e);
    }
    for (const auto& t : out.templates) {
        DupEntry e;
        e.role = EntryRole::Template;
        e.display = QStringLiteral("template (%1)").arg(t->meta.fileName);
        e.identity.fileName = t->meta.fileName;
        e.drawing = t;
        e.group = -1;
        out.dupEntries.push_back(e);
    }
    out.dups = detectDuplicates(out.dupEntries, in.settings.duplicates, cmp.mode, nullptr, viewsPtr, out.refModes);
    for (const QString& n : out.dups.notes) out.log << n;
    int failed = 0;
    for (const auto& r : out.students) failed += !r.ok();
    out.log << QStringLiteral("%1 submissions, %2 could not be read").arg(out.students.size()).arg(failed);
    out.ok = true;
    return out;
}

}  // namespace dc
