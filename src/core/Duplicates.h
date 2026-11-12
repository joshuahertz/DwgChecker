#pragma once
// Duplicate / collaboration detection. Presents evidence only: signals, weights,
// a combined score and clusters. It never labels anything as cheating.

#include "Model.h"
#include "Ortho.h"
#include "Settings.h"
#include "StudentIdentity.h"

#include <QString>
#include <QStringList>

#include <vector>

namespace dc {

enum class Strength { Info, Weak, Medium, Strong };
QString strengthName(Strength s);

// A signal's text for the table and the details: "label (a; b)", or the label alone (D82).
QString signalText(const QString& label, const QStringList& inParens);

struct Signal {
    QString id;       // "sha256", "versionguid", "fingerprint", ...
    QString label;    // short human text: "same fingerprint"
    // What it is about, shown in parentheses in the Possible cheating table (D82): the shared
    // account name, the creation time, "Drawing 2: 45 / 50"; may be empty.
    QString value;
    QString detail;   // longer explanation shown in tooltips / the Duplicates tab
    Strength strength = Strength::Info;
    double weight = 0;
    // "same account name (\"jsmith\")": the label, its value in parentheses.
    QString shown() const { return signalText(label, value.isEmpty() ? QStringList() : QStringList{value}); }
};

enum class EntryRole { Student, Reference, Template };

struct DupEntry {
    EntryRole role = EntryRole::Student;
    QString display;          // student name or file name
    StudentIdentity identity;
    DrawingPtr drawing;       // may be null when the file failed to load
    double correctness = -1;  // students only
    // Orthographic mode: per student drawing object, the reference view it was assigned to.
    std::vector<int> primView;
    // Several solution drawings: the one this file belongs to (-1 = none / any). Geometry
    // is compared only within a group; file metadata across groups too.
    int group = 0;
    // The student a file belongs to; one student's own files are never compared with each other.
    QString owner;
};

struct PairResult {
    int a = -1, b = -1;  // indices into the entry list
    std::vector<Signal> evidence;
    double score = 0;    // sum of weights, capped at 100
    Strength level = Strength::Info;
    bool flagged() const { return level >= Strength::Weak; }
    QString strongestLabel() const;
};

struct DupReport {
    std::vector<PairResult> pairs;          // only pairs with at least one signal, strongest first
    QStringList notes;                      // probable templates etc.
    std::vector<QStringList> fileNotes;     // per entry (e.g. very small TDINDWG)
    // Per entry, evidence from the student's own files rather than a pair: several account
    // names across one student's files (D77), a file created long before the class's work
    // (D86). Shown in the file's flag like a pair's signal.
    std::vector<std::vector<Signal>> fileSignals;
};

struct BitExactStats {
    int exact = 0;       // drawing objects bit-identical at T = identity
    int nontrivial = 0;  // of those, with at least one non-round coordinate
    int smaller = 0;     // drawing object count of the smaller drawing
};
BitExactStats bitExact(const Drawing& a, const Drawing& b, const std::vector<int>* onlyA = nullptr,
                       const std::vector<int>* onlyB = nullptr);

// `views`: the reference views (Orthographic); `viewsByGroup` / `modeByGroup`, when given,
// per group (solution drawing) instead.
DupReport detectDuplicates(const std::vector<DupEntry>& entries, const DuplicateSettings& s, Mode mode,
                           const ViewSet* views, const std::vector<const ViewSet*>& viewsByGroup = {},
                           const std::vector<Mode>& modeByGroup = {});

// A row of the Possible cheating table and its export (D82). One of:
// - students linked to each other: every link between them, at the strongest level of the
//   links that made the group (a weaker link to another student makes a row of its own at
//   that level, so it never pulls them into a stronger group);
// - one student linked to a solution drawing or a template (`with` names it): kept apart, so
//   that students who each copied the handout are not shown as linked to each other;
// - one student whose own files are suspect (different account names, an old file: `ownSignals`).
struct EvidenceGroup {
    Strength level = Strength::Info;
    double score = 0;        // the strongest link's (capped at 100)
    QStringList students;    // names, sorted
    QString with;            // "the solution drawing", "the template", or empty
    std::vector<int> files;  // entries: the students' files (by student), then a solution drawing or template
    std::vector<int> pairs;  // the links, indices into DupReport::pairs, strongest first
    std::vector<Signal> ownSignals;
    // The signals, each kind and value once over all the links, strongest first:
    // "same account name ("jsmith")". In a group of more than two, a signal that only some
    // of them share names them: "same creation time (2026-11-03 14:22:05 UTC; a, b)".
    struct Line {
        QString text;
        Strength strength = Strength::Info;
        double weight = 0;
    };
    std::vector<Line> lines;
};
// The groups, strongest first. `withInfo`: also groups linked by informational signals only.
std::vector<EvidenceGroup> groupEvidence(const DupReport& rep, const std::vector<DupEntry>& entries, bool withInfo);
// A student file's name, or a solution drawing's or template's with what it is: "q3.dwg
// (solution drawing)".
QString evidenceFileName(const DupEntry& e);
// The same, short, as the results name the drawings (D84): "smithjane: Drawing 2",
// "smithjane: extra file", "Solution: Drawing 2", "Template: name.dwg".
QString evidenceFileLabel(const DupEntry& e);

}  // namespace dc
