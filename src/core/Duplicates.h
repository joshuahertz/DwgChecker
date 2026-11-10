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

struct Signal {
    QString id;       // "sha256", "versionguid", "fingerprint", ...
    QString label;    // short human text: "same fingerprint"
    QString detail;   // longer explanation shown in tooltips / the Duplicates tab
    Strength strength = Strength::Info;
    double weight = 0;
};

enum class EntryRole { Student, Reference, Template };

struct DupEntry {
    EntryRole role = EntryRole::Student;
    QString display;          // student name or file name
    StudentIdentity identity;
    DrawingPtr drawing;       // may be null when the file failed to load
    double correctness = -1;  // students only
    // Orthographic mode: per student primitive, the reference view it was assigned to.
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
    std::vector<std::vector<int>> clusters; // groups joined by Strong evidence (size >= 2)
    QStringList notes;                      // probable templates etc.
    std::vector<QStringList> fileNotes;     // per entry (e.g. very small TDINDWG)
};

struct BitExactStats {
    int exact = 0;       // primitives bit-identical at T = identity
    int nontrivial = 0;  // of those, with at least one non-round coordinate
    int smaller = 0;     // primitive count of the smaller drawing
};
BitExactStats bitExact(const Drawing& a, const Drawing& b, const std::vector<int>* onlyA = nullptr,
                       const std::vector<int>* onlyB = nullptr);

// `views`: the reference views (Orthographic); `viewsByGroup` / `modeByGroup`, when given,
// per group (solution drawing) instead.
DupReport detectDuplicates(const std::vector<DupEntry>& entries, const DuplicateSettings& s, Mode mode,
                           const ViewSet* views, const std::vector<const ViewSet*>& viewsByGroup = {},
                           const std::vector<Mode>& modeByGroup = {});

}  // namespace dc
