#pragma once
// Runs a whole grading batch: the solution drawing(s), views, student files (in
// parallel), templates and duplicate detection. Runs on a worker thread in the GUI;
// the unit tests call it directly.
//
// An assignment may have several solution drawings. Student files are grouped by
// student (the name and Canvas user id in the file name), every file is compared with
// every solution drawing, and each student's files are matched to the drawings they
// fit best (a matching file name breaks near ties). A solution drawing without a file
// counts 0%; a file left over is an extra, shown but not graded.

#include "Comparator.h"
#include "Duplicates.h"
#include "Loader.h"
#include "Ortho.h"
#include "Settings.h"
#include "StudentIdentity.h"

#include <QHash>
#include <QStringList>

#include <atomic>
#include <functional>
#include <optional>
#include <vector>

namespace dc {

struct BatchInput {
    QStringList refPaths;  // one or more solution drawings
    QStringList studentPaths;
    QStringList templatePaths;
    AppSettings settings;
    // Orthographic: views confirmed in the editor, by reference path (else sidecar, else auto).
    QHash<QString, ViewSet> views;
};

// One student file.
struct StudentRun {
    QString path;
    StudentIdentity identity;
    DrawingPtr drawing;
    QString loadError;
    CompareResult result;  // against refs[refIndex]; for an extra file, against refs[closestRef]
    int refIndex = 0;      // the solution drawing it was graded against; -1 = not matched (extra file)
    int closestRef = 0;    // the solution drawing it fits best
    int group = -1;        // index into BatchOutput::groups
    bool ok() const { return drawing != nullptr; }
    bool matched() const { return refIndex >= 0; }
    QString displayName() const { return identity.name; }
};

// One student: their files, and which file was graded against each solution drawing.
struct StudentGroup {
    QString key;             // name + Canvas user id, or the file name when it does not parse
    StudentIdentity identity;
    std::vector<int> files;  // indices into BatchOutput::students
    std::vector<int> byRef;  // per solution drawing: the file graded against it, -1 = none
    double correctness = 0;  // mean over the solution drawings; a drawing without a file counts 0
};

struct BatchOutput {
    bool ok = false;
    QString error;
    bool cancelled = false;
    QString parser;             // dwgread version string
    QStringList refPaths;
    std::vector<DrawingPtr> refs;  // the solution drawings, same order as refPaths
    std::vector<Mode> refModes;    // per solution drawing (they may differ, see AppSettings::perDrawing)
    bool hasViews = false;         // some solution drawing is graded in Orthographic mode
    std::vector<ViewSet> views;    // per solution drawing; empty unless it is Orthographic
    std::vector<StudentRun> students;  // one per file
    std::vector<StudentGroup> groups;  // one per student, sorted by name
    std::vector<DrawingPtr> templates;
    QStringList log;
    // Duplicate detection: entries are the student files (same order as `students`), then the
    // solution drawings, then templates.
    std::vector<DupEntry> dupEntries;
    DupReport dups;
    Mode mode = Mode::Relative;

    bool multipleRefs() const { return refs.size() > 1; }
    // The solution drawing a file was compared with (its match, or the closest one).
    int refOf(const StudentRun& r) const { return r.matched() ? r.refIndex : r.closestRef; }
    const Drawing& refDrawing(const StudentRun& r) const { return *refs[refOf(r)]; }
    const ViewSet* viewsOf(const StudentRun& r) const {
        const int k = refOf(r);
        return hasViews && refModes[k] == Mode::Orthographic ? &views[k] : nullptr;
    }
};

using ProgressFn = std::function<void(int done, int total, const QString& what)>;

// Finds submissions: *.dwg in `dir` (not recursive), sorted by name. When there are
// none, pre-dumped *.json files are used instead (offline/testing).
QStringList findSubmissionFiles(const QString& dir);

// The key that groups a student's files: name + Canvas user id, or the file name.
QString studentKey(const StudentIdentity& id);

// One-to-one assignment of rows (one student's files) to columns (the solution drawings)
// with the largest total weight. result[row] is a column, or -1. As many rows as possible
// get a column: with more rows than columns, the rows left out are those the total misses
// least; with fewer, some columns stay free.
std::vector<int> assignOneToOne(const std::vector<std::vector<double>>& weight);

// Resolves the views to use for an Orthographic run (explicit > sidecar > auto).
ViewSet resolveReferenceViews(const Drawing& ref, const QString& refPath, const CompareSettings& s,
                              const std::optional<ViewSet>& explicitViews, QString* note);

BatchOutput runBatch(const BatchInput& in, const IDrawingLoader& loader, const ProgressFn& progress = {},
                     const std::atomic_bool* cancel = nullptr);

}  // namespace dc
