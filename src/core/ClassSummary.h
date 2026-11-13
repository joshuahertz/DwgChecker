#pragma once
// The class-wide summary of a run (D89): for each solution drawing, how the class scored, the
// most common kinds of error, and which parts of the solution were most often missed.

#include "Batch.h"

#include <QString>

#include <utility>
#include <vector>

namespace dc {

// One kind of error ("missing", "shifted", "wrong layer", as the results name them).
struct ErrorKindCount {
    QString label;
    int students = 0;         // graded files with at least one
    int times = 0;            // how many in all
    bool onSolution = false;  // it names parts of the solution ("extra" names the students' own)
};

// One part of the solution drawing.
struct SolutionPartCount {
    int prim = -1;     // index into the solution drawing's prims
    int compared = 0;  // graded files that were graded on it
    int missed = 0;    // ... that did not get it right: missing, near it, or a wrong attribute
    // The kinds of error it had, by how many files, most first: ("missing", 8), ("shifted", 3).
    std::vector<std::pair<QString, int>> how;
    double rate() const { return compared > 0 ? static_cast<double>(missed) / compared : 0.0; }
    int count(const QString& label) const;  // files with that kind of error on it
};

struct DrawingSummary {
    int ref = 0;
    int graded = 0;              // students' files graded against it
    int noFile = 0;              // students with no readable file for it
    std::vector<double> scores;  // the graded files' correctness, lowest first
    double mean = 0, median = 0;
    int withErrors = 0;          // graded files with at least one error
    int errorCount = 0;          // their errors in all
    std::vector<ErrorKindCount> kinds;     // most students first
    std::vector<SolutionPartCount> parts;  // every part that was graded, most missed first
    std::vector<int> partOf;               // per prim of the solution: its index in `parts`, or -1
};

DrawingSummary summarizeDrawing(const BatchOutput& b, int ref);

// All the drawings together (several solution drawings, D90): each student's overall score,
// the mean over the drawings with a missing one counting 0.
struct ClassOverall {
    int students = 0;
    int complete = 0;            // students with a readable file for every drawing
    std::vector<double> scores;  // overall scores, lowest first
    double mean = 0, median = 0;
};
ClassOverall summarizeClass(const BatchOutput& b);
// The summary's color scale (D90, in the core since D92 so it is tested): a part's level from
// the share of students who missed it. 0 for none, rising to 1 (yellow) at `common`, the
// highest share at or under half, so the commonest mistakes stand out even when fewer than
// half made them; 2 (red) past half. `common` is 0.5 when no share is at or under half.
double commonMissRate(const std::vector<double>& rates);
double missLevel(double rate, double common);
// What a part of the solution is, for a person: "line 2.500 long on HIDDEN (Front view)".
QString describePart(const BatchOutput& b, int ref, int prim);
// "8 missing, 3 shifted".
QString describeHow(const SolutionPartCount& p);

}  // namespace dc
