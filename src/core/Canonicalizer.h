#pragma once
// Canonicalization (spec rules 1-10): turns parsed DWG JSON into a list of
// canonical primitives so that equivalent drawings compare equal.

#include "DwgJson.h"
#include "Loader.h"
#include "Model.h"
#include "Settings.h"

#include <QRegularExpression>

namespace dc {

class LinetypeClassifier {
public:
    explicit LinetypeClassifier(const CanonSettings& s);
    LtClass classify(const QString& ltypeName) const;

private:
    std::vector<std::pair<QRegularExpression, LtClass>> m_rules;
};

// Full pipeline on parsed JSON: explode, convert, normalize, dedupe, merge.
Drawing canonicalize(const DwgJson& json, const CanonSettings& s);

// Individual post-processing passes (exposed for unit tests). They operate on
// the primitive list in place and keep the original order of survivors.
void normalizeArcs(std::vector<Primitive>& prims, const CanonSettings& s);       // rule 5
void dropDegenerate(std::vector<Primitive>& prims, const CanonSettings& s);      // rule 6 (zero length)
void removeStackedDuplicates(std::vector<Primitive>& prims, const CanonSettings& s);  // rule 6
void mergeCollinearLines(std::vector<Primitive>& prims, const CanonSettings& s);  // rule 6
void mergeCocircularArcs(std::vector<Primitive>& prims, const CanonSettings& s);  // rule 7
// Runs the passes above in order and adds the resulting warnings.
void postProcess(Drawing& d, const CanonSettings& s);

// Loader + metadata + canonicalization for one file. Returns nullptr on failure.
std::shared_ptr<Drawing> loadDrawing(const QString& path, const IDrawingLoader& loader, const CanonSettings& s,
                                     QString* error);

}  // namespace dc
