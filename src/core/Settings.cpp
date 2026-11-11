#include "Settings.h"

#include <QFileInfo>

#include <QJsonArray>

#include <algorithm>

namespace dc {

QString modeName(Mode m) {
    switch (m) {
    case Mode::Absolute: return QStringLiteral("absolute");
    case Mode::Relative: return QStringLiteral("relative");
    case Mode::Orthographic: return QStringLiteral("orthographic");
    }
    return {};
}

bool parseMode(const QString& s, Mode* out) {
    const QString l = s.trimmed().toLower();
    if (l == "absolute" || l == "abs") { *out = Mode::Absolute; return true; }
    if (l == "relative" || l == "rel") { *out = Mode::Relative; return true; }
    if (l == "orthographic" || l == "ortho") { *out = Mode::Orthographic; return true; }
    return false;
}

QString severityName(Severity s) {
    switch (s) {
    case Severity::Fail: return QStringLiteral("fail");
    case Severity::Warn: return QStringLiteral("warn");
    case Severity::Off: return QStringLiteral("off");
    }
    return {};
}

static Severity parseSeverity(const QString& s, Severity def) {
    if (s == "fail") return Severity::Fail;
    if (s == "warn") return Severity::Warn;
    if (s == "off") return Severity::Off;
    return def;
}

QString AppSettings::defaultIdentityRegex() {
    return QStringLiteral(R"(^(?<name>[a-z]+)_(?:LATE_)?(?<uid>\d+)_(?<sid>\d+)_(?<orig>.+)$)");
}

CanonSettings AppSettings::effectiveCanon() const {
    CanonSettings c = canon;
    c.linTol = compare.linTol;
    c.angTol = compare.angTol;
    c.mergeByLayer = compare.requireLayerMatch;
    c.mergeByColor = compare.matchColor;
    return c;
}

static QJsonArray toArr(const QStringList& l) { return QJsonArray::fromStringList(l); }
static QStringList fromArr(const QJsonValue& v, const QStringList& def) {
    if (!v.isArray()) return def;
    QStringList out;
    for (const auto& e : v.toArray()) out << e.toString();
    return out;
}

QJsonObject AppSettings::toJson() const {
    QJsonObject c{
        {"usePaperSpace", canon.usePaperSpace}, {"skipDefpoints", canon.skipDefpoints},
        {"layerIgnoreRegex", canon.layerIgnoreRegex}, {"skipHiddenLayers", canon.skipHiddenLayers},
        {"ignoreHiddenLayersInReference", canon.ignoreHiddenLayersInReference},
        {"hiddenPatterns", toArr(canon.hiddenPatterns)}, {"centerPatterns", toArr(canon.centerPatterns)},
        {"phantomPatterns", toArr(canon.phantomPatterns)}, {"visiblePatterns", toArr(canon.visiblePatterns)}};
    QJsonObject m{
        {"mode", modeName(compare.mode)}, {"matchLinetype", compare.matchLinetype},
        {"allowRotation", compare.allowRotation}, {"allowMirror", compare.allowMirror},
        {"linTol", compare.linTol}, {"angTol", compare.angTol},
        {"requireLayerMatch", compare.requireLayerMatch},
        {"matchColor", compare.matchColor}, {"colorTolerance", compare.colorTolerance},
        {"requireByLayer", compare.requireByLayer},
        {"extras", compare.extras == ExtrasPolicy::Penalize ? "penalize" : "report"},
        {"checkDimensions", compare.checkDimensions}, {"checkDimPrecision", compare.checkDimPrecision},
        {"strictCenterlines", compare.strictCenterlines},
        {"checkHatches", compare.checkHatches}, {"checkHatchPattern", compare.checkHatchPattern},
        {"detectScale", compare.detectScale}, {"scalePenaltyErrors", compare.scalePenaltyErrors},
        {"detectLocation", compare.detectLocation}, {"locationPenaltyErrors", compare.locationPenaltyErrors},
        {"nearMissRadius", compare.nearMissRadius}, {"orthoTopK", compare.orthoTopK},
        {"orthoGapFraction", compare.orthoGapFraction}, {"alignTol", compare.alignTol},
        {"alignmentSeverity", severityName(compare.alignmentSeverity)},
        {"arrangementSeverity", severityName(compare.arrangementSeverity)}};
    QJsonObject d{
        {"templateFiles", toArr(duplicates.templateFiles)},
        {"templateShareFraction", duplicates.templateShareFraction},
        {"smallEditMinutes", duplicates.smallEditMinutes}};
    QJsonObject l{
        {"timeoutSec", loader.timeoutSec},
        {"odaConverterPath", loader.odaConverterPath}};
    QJsonObject pd;
    for (const auto& [name, ds] : perDrawing)
        pd.insert(name, QJsonObject{{"mode", modeName(ds.mode)}, {"checkDimensions", ds.checkDimensions},
                                    {"checkHatches", ds.checkHatches}});
    return QJsonObject{{"version", kFormatVersion}, {"canon", c}, {"compare", m}, {"perDrawing", pd}, {"duplicates", d},
                       {"loader", l}, {"identityRegex", identityRegex}};
}

CanonSettings AppSettings::referenceCanon() const {
    CanonSettings c = effectiveCanon();
    if (c.ignoreHiddenLayersInReference) c.skipHiddenLayers = true;
    return c;
}

QString AppSettings::drawingKey(const QString& refPath) { return QFileInfo(refPath).fileName().toLower(); }

CompareSettings AppSettings::compareFor(const QString& refPath) const {
    CompareSettings c = compare;
    const auto it = perDrawing.find(drawingKey(refPath));
    if (it != perDrawing.end()) {
        c.mode = it->second.mode;
        c.checkDimensions = it->second.checkDimensions;
        c.checkHatches = it->second.checkHatches;
    }
    return c;
}

AppSettings AppSettings::fromJson(const QJsonObject& o) {
    AppSettings s;
    const QJsonObject c = o.value("canon").toObject();
    s.canon.usePaperSpace = c.value("usePaperSpace").toBool(s.canon.usePaperSpace);
    s.canon.skipDefpoints = c.value("skipDefpoints").toBool(s.canon.skipDefpoints);
    s.canon.layerIgnoreRegex = c.value("layerIgnoreRegex").toString(s.canon.layerIgnoreRegex);
    s.canon.skipHiddenLayers = c.value("skipHiddenLayers").toBool(s.canon.skipHiddenLayers);
    s.canon.ignoreHiddenLayersInReference = c.value("ignoreHiddenLayersInReference").toBool(s.canon.ignoreHiddenLayersInReference);
    s.canon.hiddenPatterns = fromArr(c.value("hiddenPatterns"), s.canon.hiddenPatterns);
    s.canon.centerPatterns = fromArr(c.value("centerPatterns"), s.canon.centerPatterns);
    s.canon.phantomPatterns = fromArr(c.value("phantomPatterns"), s.canon.phantomPatterns);
    s.canon.visiblePatterns = fromArr(c.value("visiblePatterns"), s.canon.visiblePatterns);

    const QJsonObject m = o.value("compare").toObject();
    parseMode(m.value("mode").toString(modeName(s.compare.mode)), &s.compare.mode);
    s.compare.matchLinetype = m.value("matchLinetype").toBool(s.compare.matchLinetype);
    s.compare.allowRotation = m.value("allowRotation").toBool(s.compare.allowRotation);
    s.compare.allowMirror = m.value("allowMirror").toBool(s.compare.allowMirror);
    s.compare.linTol = m.value("linTol").toDouble(s.compare.linTol);
    s.compare.angTol = m.value("angTol").toDouble(s.compare.angTol);
    s.compare.requireLayerMatch = m.value("requireLayerMatch").toBool(s.compare.requireLayerMatch);
    s.compare.matchColor = m.value("matchColor").toBool(s.compare.matchColor);
    s.compare.colorTolerance = std::clamp(m.value("colorTolerance").toDouble(s.compare.colorTolerance), 0.0, 100.0);
    s.compare.requireByLayer = m.value("requireByLayer").toBool(s.compare.requireByLayer);
    s.compare.extras = m.value("extras").toString() == "report" ? ExtrasPolicy::ReportOnly : ExtrasPolicy::Penalize;
    s.compare.checkDimensions = m.value("checkDimensions").toBool(s.compare.checkDimensions);
    s.compare.checkDimPrecision = m.value("checkDimPrecision").toBool(s.compare.checkDimPrecision);
    s.compare.strictCenterlines = m.value("strictCenterlines").toBool(s.compare.strictCenterlines);
    s.compare.checkHatches = m.value("checkHatches").toBool(s.compare.checkHatches);
    s.compare.checkHatchPattern = m.value("checkHatchPattern").toBool(s.compare.checkHatchPattern);
    s.compare.detectScale = m.value("detectScale").toBool(s.compare.detectScale);
    s.compare.scalePenaltyErrors = std::max(0, m.value("scalePenaltyErrors").toInt(s.compare.scalePenaltyErrors));
    s.compare.detectLocation = m.value("detectLocation").toBool(s.compare.detectLocation);
    s.compare.locationPenaltyErrors = std::max(0, m.value("locationPenaltyErrors").toInt(s.compare.locationPenaltyErrors));
    s.compare.nearMissRadius = m.value("nearMissRadius").toDouble(s.compare.nearMissRadius);
    s.compare.orthoTopK = m.value("orthoTopK").toInt(s.compare.orthoTopK);
    s.compare.orthoGapFraction = m.value("orthoGapFraction").toDouble(s.compare.orthoGapFraction);
    s.compare.alignTol = m.value("alignTol").toDouble(s.compare.alignTol);
    s.compare.alignmentSeverity = parseSeverity(m.value("alignmentSeverity").toString(), s.compare.alignmentSeverity);
    s.compare.arrangementSeverity = parseSeverity(m.value("arrangementSeverity").toString(), s.compare.arrangementSeverity);

    const QJsonObject pd = o.value("perDrawing").toObject();
    for (auto it = pd.begin(); it != pd.end(); ++it) {
        const QJsonObject e = it.value().toObject();
        DrawingSettings ds;
        ds.mode = s.compare.mode;
        parseMode(e.value("mode").toString(), &ds.mode);
        ds.checkDimensions = e.value("checkDimensions").toBool(s.compare.checkDimensions);
        ds.checkHatches = e.value("checkHatches").toBool(s.compare.checkHatches);
        s.perDrawing[it.key().toLower()] = ds;
    }

    const QJsonObject d = o.value("duplicates").toObject();
    s.duplicates.templateFiles = fromArr(d.value("templateFiles"), s.duplicates.templateFiles);
    s.duplicates.templateShareFraction = d.value("templateShareFraction").toDouble(s.duplicates.templateShareFraction);
    s.duplicates.smallEditMinutes = d.value("smallEditMinutes").toDouble(s.duplicates.smallEditMinutes);

    const QJsonObject l = o.value("loader").toObject();
    s.loader.timeoutSec = l.value("timeoutSec").toInt(s.loader.timeoutSec);
    s.loader.odaConverterPath = l.value("odaConverterPath").toString(s.loader.odaConverterPath);

    s.identityRegex = o.value("identityRegex").toString(s.identityRegex);
    if (s.identityRegex.isEmpty()) s.identityRegex = defaultIdentityRegex();
    // Before version 2 the pattern check was off by default and was saved as off
    // whenever hatches were off, so a stored "false" says nothing about a choice.
    if (o.value("version").toInt(1) < 2) s.compare.checkHatchPattern = CompareSettings{}.checkHatchPattern;
    return s;
}

}  // namespace dc
