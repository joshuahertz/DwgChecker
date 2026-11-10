#pragma once
// Orthographic mode: reference segmentation into views, the view sidecar file,
// and joint per-view alignment against the whole (unsegmented) student drawing.

#include "Aligner.h"
#include "Model.h"
#include "Settings.h"

#include <QString>
#include <QStringList>

#include <vector>

namespace dc {

// Ignored: a part of the reference that is located like a view (so the student's copy of it
// is not extra) but never graded, such as a title block, notes or construction.
enum class ViewName { Front, Top, Right, Left, Bottom, Rear, Other, Ignored };
QString viewNameString(ViewName n);
bool parseViewName(const QString& s, ViewName* out);
const QList<ViewName>& allViewNames();

struct ViewDef {
    int id = 0;
    ViewName name = ViewName::Other;
    BBox bbox;               // of the view's primitives, reference frame
    std::vector<int> prims;  // indices into the reference Drawing::prims
};

struct ViewSet {
    std::vector<ViewDef> views;
    std::vector<int> primView;  // per reference primitive, -1 = none
    QStringList primIds;        // source id of each reference primitive when primView was built
    QByteArray refSha256;
    double gapFraction = 0.02;
    QString source;             // "auto" or the sidecar path

    int indexOfName(ViewName n) const;
    bool isIgnored(int viewIdx) const { return viewIdx >= 0 && viewIdx < static_cast<int>(views.size()) && views[viewIdx].name == ViewName::Ignored; }
    QString label(int viewIdx) const;  // "Front view"
    // Recomputes prims lists and boxes from primView; unassigned primitives go to the nearest view.
    void rebuild(const Drawing& ref);
    // The same views applied to `ref`, whose primitive list may differ (settings changed):
    // primitives are matched by source id, the rest go to the nearest view.
    ViewSet rebindTo(const Drawing& ref) const;
};

// Single-linkage clustering of Visible/Hidden/Other primitives (gap = fraction of
// the drawing diagonal), then Center/Phantom/annotation attached to the nearest view.
ViewSet segmentViews(const Drawing& ref, double gapFraction);
// Third-angle defaults: Front has views above and to its right.
void guessViewNames(ViewSet& vs);

QString sidecarPath(const QString& refPath);  // "<ref>.views.json"
bool saveSidecar(const QString& path, const ViewSet& vs, const Drawing& ref, QString* error);
// Loads and validates the sidecar against the reference SHA-256. Primitives are
// matched by source id; unknown ones go to the nearest saved view box.
bool loadSidecar(const QString& path, const Drawing& ref, ViewSet* out, QString* error);

struct ViewCandidate {
    Transform2D T;  // translation, student -> reference
    int score = 0;
    std::vector<int> claims;  // student primitives matched (geometry only)
};

struct OrthoResolution {
    std::vector<std::vector<ViewCandidate>> candidates;  // per view, best first
    std::vector<int> chosen;  // per view: candidate index, -1 = view not found
    int combinationsTried = 0;
};

struct AlignmentCheck {
    int view = -1;
    bool isArrangement = false;
    bool applicable = false;
    bool passed = true;
    QString text;  // failure description
};

// Projection alignment and arrangement checks from per-view translations.
std::vector<AlignmentCheck> orthoChecks(const ViewSet& vs, const std::vector<const Transform2D*>& viewT, double alignTol);

OrthoResolution resolveViews(const std::vector<Primitive>& ref, const ViewSet& vs, const std::vector<Primitive>& stu,
                             const std::vector<int>& stuIdx, const CompareSettings& s, const MatchCriteria& crit);

}  // namespace dc
