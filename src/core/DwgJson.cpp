#include "DwgJson.h"

#include "Color.h"

#include <QJsonDocument>
#include <QJsonParseError>
#include <QRegularExpression>

#include <algorithm>

namespace dc {

quint64 DwgJson::href(const QJsonValue& v) {
    if (!v.isArray()) return 0;
    const QJsonArray a = v.toArray();
    if (a.isEmpty()) return 0;
    return static_cast<quint64>(a.last().toDouble());
}

quint64 DwgJson::ownHandle(const QJsonObject& o) { return href(o.value("handle")); }

Vec3 DwgJson::vec3(const QJsonValue& v, Vec3 def) {
    if (!v.isArray()) return def;
    const QJsonArray a = v.toArray();
    if (a.size() < 2) return def;
    return {a.at(0).toDouble(), a.at(1).toDouble(), a.size() > 2 ? a.at(2).toDouble() : 0.0};
}

Vec2 DwgJson::vec2(const QJsonValue& v, Vec2 def) {
    if (!v.isArray()) return def;
    const QJsonArray a = v.toArray();
    if (a.size() < 2) return def;
    return {a.at(0).toDouble(), a.at(1).toDouble()};
}

double DwgJson::num(const QJsonObject& o, const char* key, double def) {
    const QJsonValue v = o.value(QLatin1StringView(key));
    return v.isDouble() ? v.toDouble() : def;
}

int DwgJson::integer(const QJsonObject& o, const char* key, int def) {
    const QJsonValue v = o.value(QLatin1StringView(key));
    return v.isDouble() ? static_cast<int>(v.toDouble()) : def;
}

static QByteArray sanitize(const QByteArray& in, bool* nonFinite) {
    // Non-release LibreDWG builds can print bare nan/inf, which is not JSON. They become null, not
    // 0.0, which would put geometry at the origin (D76).
    QString s = QString::fromUtf8(in);
    static const QRegularExpression bad(QStringLiteral(R"((?<=[\[:,\s])-?(nan|inf|infinity)(?=[\],}\s]))"),
                                        QRegularExpression::CaseInsensitiveOption);
    *nonFinite = s.contains(bad);
    s.replace(bad, QStringLiteral("null"));
    // Control characters inside strings.
    for (QChar& c : s)
        if (c.unicode() < 0x20 && c != '\n' && c != '\r' && c != '\t') c = QChar(' ');
    return s.toUtf8();
}

bool DwgJson::hasNull(const QJsonValue& v) {
    if (v.isNull()) return true;
    if (v.isArray()) {
        for (const auto& e : v.toArray())
            if (hasNull(e)) return true;
    } else if (v.isObject()) {
        const QJsonObject o = v.toObject();
        for (auto it = o.begin(); it != o.end(); ++it)
            if (hasNull(it.value())) return true;
    }
    return false;
}

bool DwgJson::parse(const QByteArray& json, QString* error) {
    QJsonParseError pe;
    QJsonDocument doc = QJsonDocument::fromJson(json, &pe);
    m_nonFinite = false;
    if (pe.error != QJsonParseError::NoError) {
        const QString first = pe.errorString() + QStringLiteral(" at offset %1").arg(pe.offset);
        doc = QJsonDocument::fromJson(sanitize(json, &m_nonFinite), &pe);
        if (pe.error != QJsonParseError::NoError) {
            if (error) *error = QStringLiteral("invalid JSON from parser: %1").arg(first);
            return false;
        }
    }
    if (!doc.isObject()) {
        if (error) *error = QStringLiteral("parser output is not a JSON object");
        return false;
    }
    return setRoot(doc.object(), error);
}

bool DwgJson::setRoot(const QJsonObject& root, QString* error) {
    m_root = root;
    m_header = root.value("HEADER").toObject();
    if (!root.value("OBJECTS").isArray()) {
        if (error) *error = QStringLiteral("parser output has no OBJECTS array");
        return false;
    }
    index();
    return true;
}

QString DwgJson::typeOf(const QJsonObject& o) const {
    const QJsonValue e = o.value("entity");
    if (e.isString()) return e.toString();
    return o.value("object").toString();
}

void DwgJson::index() {
    m_objects.clear(); m_byHandle.clear(); m_layers.clear(); m_layerByName.clear();
    m_ltypes.clear(); m_blocks.clear(); m_model.clear(); m_paper.clear(); m_ownedBy.clear();
    m_sortKeys.clear(); m_modelBlock = m_paperBlock = 0;

    const QJsonArray arr = m_root.value("OBJECTS").toArray();
    m_objects.reserve(arr.size());
    for (const auto& v : arr) m_objects.push_back(v.toObject());

    for (int i = 0; i < static_cast<int>(m_objects.size()); ++i) {
        const QJsonObject& o = m_objects[i];
        const quint64 h = ownHandle(o);
        if (h) m_byHandle.insert(h, i);
        const bool isEntity = o.value("entity").isString();
        const QString type = typeOf(o);
        if (!isEntity) {
            if (type == "LAYER") {
                Layer l;
                l.handle = h;
                l.name = o.value("name").toString();
                l.ltype = href(o.value("ltype"));
                l.flag0 = integer(o, "flag0", 16);
                const DwgColor col = parseDwgColor(o.value("color"));
                l.color = col.kind == DwgColor::Kind::Rgb ? -1 : col.kind == DwgColor::Kind::Aci ? col.aci : 7;
                l.rgb = col.kind == DwgColor::Kind::Rgb ? col.rgb : aciToRgb(l.color);
                m_layers.insert(h, l);
                m_layerByName.insert(l.name.toUpper(), h);
            } else if (type == "LTYPE") {
                m_ltypes.insert(h, o.value("name").toString());
            } else if (type == "BLOCK_HEADER") {
                Block b;
                b.handle = h;
                b.name = o.value("name").toString();
                b.base = vec3(o.value("base_pt"));
                b.anonymous = integer(o, "anonymous") != 0;
                b.xref = integer(o, "blkisxref") != 0;
                for (const auto& e : o.value("entities").toArray()) b.entities.push_back(href(e));
                if (b.name.compare(QLatin1String("*Model_Space"), Qt::CaseInsensitive) == 0) m_modelBlock = h;
                if (b.name.compare(QLatin1String("*Paper_Space"), Qt::CaseInsensitive) == 0) m_paperBlock = h;
                m_blocks.insert(h, b);
            } else if (type == "SORTENTSTABLE") {
                // Pairs by position: ents[k] is drawn as if its handle were sort_ents[k].
                const QJsonArray ents = o.value("ents").toArray(), sorts = o.value("sort_ents").toArray();
                QHash<quint64, quint64>& keys = m_sortKeys[href(o.value("block_owner"))];
                for (qsizetype k = 0; k < std::min(ents.size(), sorts.size()); ++k)
                    if (const quint64 e = href(ents[k]), sk = href(sorts[k]); e && sk) keys.insert(e, sk);
            }
            continue;
        }
        // Entities: entmode 2 = model space, 1 = paper space, 0 = owned (ownerhandle).
        const int entmode = integer(o, "entmode", 0);
        static const QStringList skipTop = {"BLOCK", "ENDBLK", "SEQEND", "VERTEX_2D", "VERTEX_3D",
                                            "VERTEX_MESH", "VERTEX_PFACE", "VERTEX_PFACE_FACE", "ATTRIB"};
        if (entmode == 2 || entmode == 1) {
            if (!skipTop.contains(type)) (entmode == 2 ? m_model : m_paper).push_back(h);
        } else {
            const quint64 owner = href(o.value("ownerhandle"));
            if (owner) m_ownedBy[owner].push_back(h);
        }
    }
    // R2000 files have no BLOCK_HEADER.entities array: fall back to the owner scan.
    for (auto it = m_blocks.begin(); it != m_blocks.end(); ++it) {
        if (it->entities.empty()) {
            const auto owned = m_ownedBy.value(it->handle);
            it->entities = owned;
        }
    }
}

std::vector<quint64> DwgJson::drawOrder(const std::vector<quint64>& ents, quint64 blockHandle) const {
    const auto it = m_sortKeys.constFind(blockHandle);
    if (!blockHandle || it == m_sortKeys.constEnd() || it->isEmpty()) return ents;
    std::vector<quint64> out = ents;
    const QHash<quint64, quint64>& keys = *it;
    std::stable_sort(out.begin(), out.end(), [&](quint64 a, quint64 b) { return keys.value(a, a) < keys.value(b, b); });
    return out;
}

const QJsonObject* DwgJson::object(quint64 handle) const {
    const auto it = m_byHandle.constFind(handle);
    if (it == m_byHandle.constEnd()) return nullptr;
    return &m_objects[*it];
}

const DwgJson::Layer* DwgJson::layer(quint64 handle) const {
    const auto it = m_layers.constFind(handle);
    return it == m_layers.constEnd() ? nullptr : &*it;
}

const DwgJson::Layer* DwgJson::layerByName(const QString& name) const {
    const auto it = m_layerByName.constFind(name.toUpper());
    return it == m_layerByName.constEnd() ? nullptr : layer(*it);
}

QString DwgJson::ltypeName(quint64 handle) const { return m_ltypes.value(handle); }

const DwgJson::Block* DwgJson::block(quint64 handle) const {
    const auto it = m_blocks.constFind(handle);
    return it == m_blocks.constEnd() ? nullptr : &*it;
}

std::vector<quint64> DwgJson::polylineVertices(const QJsonObject& pline) const {
    std::vector<quint64> out;
    for (const auto& v : pline.value("vertex").toArray()) out.push_back(href(v));
    if (out.empty()) {
        for (quint64 h : m_ownedBy.value(ownHandle(pline))) {
            const QJsonObject* o = object(h);
            if (o && typeOf(*o).startsWith("VERTEX")) out.push_back(h);
        }
    }
    return out;
}

}  // namespace dc
