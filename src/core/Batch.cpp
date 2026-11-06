#include "Batch.h"

#include "Canonicalizer.h"

#include <QDir>
#include <QFileInfo>
#include <QMutex>
#include <QThread>
#include <QThreadPool>

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

BatchOutput runBatch(const BatchInput& in, const IDrawingLoader& loader, const ProgressFn& progress,
                     const std::atomic_bool* cancel) {
    BatchOutput out;
    out.mode = in.settings.compare.mode;
    out.parser = loader.parserVersion();
    out.log << QStringLiteral("parser: %1").arg(out.parser);
    const CanonSettings canon = in.settings.effectiveCanon();
    const CompareSettings& cmp = in.settings.compare;
    const int total = static_cast<int>(in.studentPaths.size() + in.templatePaths.size()) + 1;
    std::atomic_int done{0};
    QMutex progressMutex;
    auto report = [&](const QString& what) {
        const int d = ++done;
        if (progress) {
            QMutexLocker lock(&progressMutex);
            progress(d, total, what);
        }
    };

    QString err;
    auto ref = loadDrawing(in.refPath, loader, canon, &err);
    report(QFileInfo(in.refPath).fileName());
    if (!ref) {
        out.error = QStringLiteral("reference %1: %2").arg(in.refPath, err);
        return out;
    }
    out.ref = ref;
    out.log << QStringLiteral("reference: %1 (sha256 %2, %3 primitives)")
                   .arg(in.refPath, QString(ref->meta.sha256.left(16))).arg(ref->prims.size());

    const ViewSet* viewsPtr = nullptr;
    if (cmp.mode == Mode::Orthographic) {
        QString note;
        out.views = resolveReferenceViews(*ref, in.refPath, cmp, in.views, &note);
        out.hasViews = true;
        viewsPtr = &out.views;
        out.log << note;
    }

    const IdentityParser idp(in.settings.identityRegex);
    out.students.resize(in.studentPaths.size());
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
                    r.result = compareDrawings(*ref, *d, cmp, viewsPtr);
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

    // Duplicate detection: students, then the reference, then templates.
    for (const StudentRun& r : out.students) {
        DupEntry e;
        e.role = EntryRole::Student;
        e.display = r.identity.name;
        e.identity = r.identity;
        e.drawing = r.drawing;
        e.correctness = r.ok() ? r.result.correctness : -1;
        if (r.ok()) e.primView = r.result.stuView;
        out.dupEntries.push_back(e);
    }
    {
        DupEntry e;
        e.role = EntryRole::Reference;
        e.display = QStringLiteral("reference (%1)").arg(QFileInfo(in.refPath).fileName());
        e.identity.fileName = QFileInfo(in.refPath).fileName();
        e.drawing = ref;
        out.dupEntries.push_back(e);
    }
    for (const auto& t : out.templates) {
        DupEntry e;
        e.role = EntryRole::Template;
        e.display = QStringLiteral("template (%1)").arg(t->meta.fileName);
        e.identity.fileName = t->meta.fileName;
        e.drawing = t;
        out.dupEntries.push_back(e);
    }
    out.dups = detectDuplicates(out.dupEntries, in.settings.duplicates, cmp.mode, viewsPtr);
    for (const QString& n : out.dups.notes) out.log << n;
    int failed = 0;
    for (const auto& r : out.students) failed += !r.ok();
    out.log << QStringLiteral("%1 submissions, %2 could not be read").arg(out.students.size()).arg(failed);
    out.ok = true;
    return out;
}

}  // namespace dc
