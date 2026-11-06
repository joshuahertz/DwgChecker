#include "MetaExtractor.h"

#include <QRegularExpression>

namespace dc {

DwgTime parseDwgTime(const QJsonValue& v) {
    DwgTime t;
    if (!v.isArray()) return t;
    const QJsonArray a = v.toArray();
    if (a.size() < 2) return t;
    t.day = static_cast<qint64>(a.at(0).toDouble());
    t.ms = static_cast<qint64>(a.at(1).toDouble());
    t.valid = t.day > 0;
    return t;
}

static double durationSeconds(const QJsonValue& v) {
    if (!v.isArray()) return -1;
    const QJsonArray a = v.toArray();
    if (a.size() < 2) return -1;
    return a.at(0).toDouble() * 86400.0 + a.at(1).toDouble() / 1000.0;
}

static QString attr(const QString& text, const QString& name) {
    const QRegularExpression re(QStringLiteral("%1\\s*=\\s*\"([^\"]*)\"").arg(QRegularExpression::escape(name)));
    const auto m = re.match(text);
    return m.hasMatch() ? m.captured(1) : QString();
}

void extractMeta(const DwgJson& json, DrawingMeta* m) {
    const QJsonObject fh = json.fileHeader();
    const QJsonObject h = json.header();
    m->dwgVersion = fh.value("version").toString();
    m->dwgVersionName = dwgVersionName(m->dwgVersion);

    // HEADER.TDCREATE/TDUPDATE are absent in dwgread 0.14 output; SummaryInfo carries them (R2004+).
    const QJsonObject si = json.summaryInfo();
    m->hasSummaryInfo = json.hasSummaryInfo();
    m->tdcreate = parseDwgTime(h.contains("TDCREATE") ? h.value("TDCREATE") : si.value("TDCREATE"));
    m->tdupdate = parseDwgTime(h.contains("TDUPDATE") ? h.value("TDUPDATE") : si.value("TDUPDATE"));
    m->tducreate = parseDwgTime(h.value("TDUCREATE"));
    m->tduupdate = parseDwgTime(h.value("TDUUPDATE"));
    m->tdindwgSeconds = durationSeconds(h.contains("TDINDWG") ? h.value("TDINDWG") : si.value("TDINDWG"));
    m->fingerprintGuid = h.value("FINGERPRINTGUID").toString();
    m->versionGuid = h.value("VERSIONGUID").toString();
    {
        const QJsonArray hs = h.value("HANDSEED").toArray();
        if (hs.size() >= 3) m->handseed = static_cast<quint64>(hs.at(2).toDouble());
    }
    m->insUnits = h.contains("INSUNITS") ? h.value("INSUNITS").toInt(-1) : -1;

    m->title = si.value("TITLE").toString();
    m->subject = si.value("SUBJECT").toString();
    m->author = si.value("AUTHOR").toString();
    m->keywords = si.value("KEYWORDS").toString();
    m->comments = si.value("COMMENTS").toString();
    m->lastSavedBy = si.value("LASTSAVEDBY").toString();
    m->revisionNumber = si.value("REVISIONNUMBER").toString();
    m->hyperlinkBase = si.value("HYPERLINKBASE").toString();
    m->customProps.clear();
    for (const auto& pv : si.value("props").toArray()) {
        const QJsonObject p = pv.toObject();
        m->customProps.append({p.value("tag").toString(), p.value("value").toString()});
    }

    const QJsonObject ai = json.appInfo();
    const QString comment = ai.value("comment").toString();
    m->appName = attr(comment, QStringLiteral("name"));
    m->appBuildVersion = attr(comment, QStringLiteral("build_version"));
    m->appRegistryVersion = attr(comment, QStringLiteral("registry_version"));
    m->appInfoText = ai.value("version").toString();
    if (m->appName.isEmpty() && !comment.isEmpty()) m->appName = comment.left(80);

    m->entityCount = static_cast<int>(json.modelSpace().size());
}

}  // namespace dc
