#pragma once
// CSV export and the per-student "copy flag" summary.

#include "Batch.h"

#include <QString>

namespace dc {

struct CopyFlag {
    Strength level = Strength::Info;  // Info = none
    QString text;                     // "⚠ same fingerprint as smithjane"
    QString tooltip;                  // every signal that fired
    int pairIndex = -1;               // strongest pair in BatchOutput::dups.pairs
    QString levelText() const;        // none / weak / strong
};

CopyFlag copyFlagFor(const BatchOutput& b, int studentIndex);
QString fileColumnText(const StudentRun& r);
QString correctnessText(const StudentRun& r);

QString csvEscape(const QString& s);
// Columns: File, Copy flag, Correctness, Errors (joined by "; ").
QString resultsCsv(const BatchOutput& b);
QString duplicatesCsv(const BatchOutput& b);

}  // namespace dc
