#pragma once
// Runs a whole grading batch: reference, views, students (in parallel), templates
// and duplicate detection. Runs on a worker thread in the GUI; the unit tests call it directly.

#include "Comparator.h"
#include "Duplicates.h"
#include "Loader.h"
#include "Ortho.h"
#include "Settings.h"
#include "StudentIdentity.h"

#include <QStringList>

#include <atomic>
#include <functional>
#include <optional>
#include <vector>

namespace dc {

struct BatchInput {
    QString refPath;
    QStringList studentPaths;
    QStringList templatePaths;
    AppSettings settings;
    std::optional<ViewSet> views;  // Orthographic: views confirmed in the editor (else sidecar, else auto)
};

struct StudentRun {
    QString path;
    StudentIdentity identity;
    DrawingPtr drawing;
    QString loadError;
    CompareResult result;
    bool ok() const { return drawing != nullptr; }
    QString displayName() const { return identity.name; }
};

struct BatchOutput {
    bool ok = false;
    QString error;
    bool cancelled = false;
    QString parser;             // dwgread version string
    DrawingPtr ref;
    bool hasViews = false;
    ViewSet views;
    std::vector<StudentRun> students;
    std::vector<DrawingPtr> templates;
    QStringList log;
    // Duplicate detection: entries are students (same order as `students`), then the reference, then templates.
    std::vector<DupEntry> dupEntries;
    DupReport dups;
    Mode mode = Mode::Relative;
};

using ProgressFn = std::function<void(int done, int total, const QString& what)>;

// Finds submissions: *.dwg in `dir` (not recursive), sorted by name. When there are
// none, pre-dumped *.json files are used instead (offline/testing).
QStringList findSubmissionFiles(const QString& dir);

// Resolves the views to use for an Orthographic run (explicit > sidecar > auto).
ViewSet resolveReferenceViews(const Drawing& ref, const QString& refPath, const CompareSettings& s,
                              const std::optional<ViewSet>& explicitViews, QString* note);

BatchOutput runBatch(const BatchInput& in, const IDrawingLoader& loader, const ProgressFn& progress = {},
                     const std::atomic_bool* cancel = nullptr);

}  // namespace dc
