#pragma once
// All user-adjustable settings, with the defaults from the spec. Serializable to
// JSON so the GUI can persist them in QSettings and save/load settings files.

#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace dc {

enum class Mode { Absolute, Relative, Orthographic };
enum class ExtrasPolicy { Penalize, ReportOnly };
enum class Severity { Fail, Warn, Off };

QString modeName(Mode m);
bool parseMode(const QString& s, Mode* out);
QString severityName(Severity s);

struct CanonSettings {
    bool usePaperSpace = false;     // rule 1: model space only by default
    bool skipDefpoints = true;      // rule 1
    QString layerIgnoreRegex;       // rule 9 (empty = none), e.g. "^(CONST|CONSTRUCTION)"
    bool skipHiddenLayers = false;  // drop entities on frozen/off layers (default: keep, with a warning)
    // rule 10: linetype name patterns (wildcards, case-insensitive)
    QStringList hiddenPatterns{QStringLiteral("HIDDEN*"), QStringLiteral("DASHED*")};
    QStringList centerPatterns{QStringLiteral("CENTER*")};
    QStringList phantomPatterns{QStringLiteral("PHANTOM*")};
    QStringList visiblePatterns{QStringLiteral("CONTINUOUS")};
    double linTol = 1e-4;           // merging / duplicate tolerance (copied from CompareSettings)
    double angTol = 1e-6;
    bool mergeByLayer = false;      // only merge lines on the same layer (when layers must match)
};

struct CompareSettings {
    Mode mode = Mode::Relative;
    bool matchLinetype = true;
    bool allowRotation = true;      // Relative mode
    bool allowMirror = false;
    double linTol = 1e-4;           // drawing units
    double angTol = 1e-6;           // radians
    bool requireLayerMatch = false;
    ExtrasPolicy extras = ExtrasPolicy::Penalize;
    bool checkDimensions = false;   // compare DIMENSION measured values
    bool checkDimPrecision = true;  // ...and the precision they show (only when checkDimensions)
    bool strictCenterlines = false; // also compare centerline length
    bool checkHatches = false;      // check that each hatched reference region is hatched
    bool checkHatchPattern = true;  // ...with the same pattern (only when checkHatches)
    double nearMissRadius = 0;      // 0 = auto: 10% of the reference diagonal
    bool detectScale = true;        // when little matches, try uniform scale factors (mm vs inches...)
    int scalePenaltyErrors = 1;     // a wrongly scaled drawing counts as this many errors
    // Orthographic
    int orthoTopK = 5;
    double orthoGapFraction = 0.02; // segmentation gap, fraction of drawing diagonal
    double alignTol = 0;            // 0 = linTol
    Severity alignmentSeverity = Severity::Fail;
    Severity arrangementSeverity = Severity::Fail;
};

struct DuplicateSettings {
    QStringList templateFiles;            // starter/template DWGs; their fingerprints are whitelisted
    double templateShareFraction = 0.30;  // a value shared by more than this fraction is "probably a template"
    double smallEditMinutes = 2.0;        // TDINDWG below this for a mostly-correct drawing is noted
};

struct LoaderSettings {
    QString dwgreadPath;                  // set at run time to the bundled copy; never saved
    int timeoutSec = 60;
    QString odaConverterPath;             // optional fallback (ODA File Converter)
};

struct AppSettings {
    CanonSettings canon;
    CompareSettings compare;
    DuplicateSettings duplicates;
    LoaderSettings loader;
    QString identityRegex = defaultIdentityRegex();

    // Written as "version". 2: the hatch pattern check defaults on; settings saved
    // before that (no version) take the new default.
    static constexpr int kFormatVersion = 2;

    static QString defaultIdentityRegex();
    // Copies the shared tolerances into canon (call before canonicalizing).
    CanonSettings effectiveCanon() const;

    QJsonObject toJson() const;
    static AppSettings fromJson(const QJsonObject& o);
};

}  // namespace dc
