#pragma once
// Helpers for building synthetic dwgread-shaped JSON in tests. The shapes mirror
// FIELDMAP.md (handles as [code, size, value, absolute], entmode, ltype_flags...).

#include "Canonicalizer.h"
#include "DwgJson.h"
#include "Loader.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include <cmath>
#include <vector>

#ifndef DWGCHECK_FIXTURES
#define DWGCHECK_FIXTURES "tests/fixtures"
#endif

namespace dct {

using dc::Vec2;
using dc::Vec3;

inline QString fixture(const QString& name) { return QStringLiteral(DWGCHECK_FIXTURES "/") + name + ".json"; }

inline std::shared_ptr<dc::Drawing> loadFixture(const QString& name, const dc::CanonSettings& s = {}) {
    dc::JsonFileLoader loader;
    QString err;
    auto d = dc::loadDrawing(fixture(name), loader, s, &err);
    if (!d) qWarning("fixture %s: %s", qPrintable(name), qPrintable(err));
    return d;
}

inline QJsonArray href(quint64 h, int code = 5) { return {code, 1, static_cast<double>(h), static_cast<double>(h)}; }
inline QJsonArray pt(double x, double y, double z = 0) { return {x, y, z}; }
inline QJsonArray pt(Vec2 p) { return {p.x, p.y, 0.0}; }
inline double deg(double d) { return d * dc::kPi / 180.0; }

class DocBuilder {
public:
    DocBuilder() {
        ltByBlock = addObject("LTYPE", {{"name", "ByBlock"}});
        ltByLayer = addObject("LTYPE", {{"name", "ByLayer"}});
        ltContinuous = addObject("LTYPE", {{"name", "Continuous"}});
        layer0 = layer("0");
    }

    quint64 ltype(const QString& name) {
        for (const auto& [n, h] : m_ltypes)
            if (n.compare(name, Qt::CaseInsensitive) == 0) return h;
        const quint64 h = addObject("LTYPE", {{"name", name}});
        m_ltypes.push_back({name, h});
        return h;
    }
    quint64 layer(const QString& name, const QString& lt = "Continuous", int flag0 = 16 + (31 << 5)) {
        const quint64 lth = lt.compare("Continuous", Qt::CaseInsensitive) == 0 ? ltContinuous : ltype(lt);
        const quint64 h = addObject("LAYER", {{"name", name}, {"ltype", href(lth)}, {"flag0", flag0},
                                              {"color", QJsonObject{{"index", 7}}}});
        m_layers.push_back({name, h});
        return h;
    }
    quint64 layerHandle(const QString& name) {
        for (const auto& [n, h] : m_layers)
            if (n == name) return h;
        return layer(name);
    }

    // Generic entity. `extra` holds the type-specific fields.
    quint64 entity(const QString& type, const QString& layerName, QJsonObject extra, const QString& lt = QString()) {
        QJsonObject o = extra;
        o.insert("entity", type);
        o.insert("layer", href(layerHandle(layerName)));
        if (lt.isEmpty()) o.insert("ltype_flags", 0);
        else if (lt.compare("ByBlock", Qt::CaseInsensitive) == 0) o.insert("ltype_flags", 1);
        else if (lt.compare("Continuous", Qt::CaseInsensitive) == 0) o.insert("ltype_flags", 2);
        else { o.insert("ltype_flags", 3); o.insert("ltype", href(ltype(lt))); }
        if (m_blockOpen) {
            o.insert("entmode", 0);
            o.insert("ownerhandle", QJsonArray{12, 1, 1, static_cast<double>(m_blockHandle)});
        } else {
            o.insert("entmode", m_paper ? 1 : 2);
        }
        if (!o.contains("extrusion")) o.insert("extrusion", pt(0, 0, 1));
        const quint64 h = add(o);
        if (m_blockOpen) m_blockEntities.push_back(h);
        return h;
    }
    quint64 line(Vec2 a, Vec2 b, const QString& lay = "0", const QString& lt = QString()) {
        return entity("LINE", lay, {{"start", pt(a)}, {"end", pt(b)}}, lt);
    }
    quint64 line3(Vec3 a, Vec3 b, const QString& lay = "0") {
        return entity("LINE", lay, {{"start", pt(a.x, a.y, a.z)}, {"end", pt(b.x, b.y, b.z)}});
    }
    quint64 circle(Vec2 c, double r, const QString& lay = "0", const QString& lt = QString(), Vec3 ext = {0, 0, 1}) {
        return entity("CIRCLE", lay, {{"center", pt(c)}, {"radius", r}, {"extrusion", pt(ext.x, ext.y, ext.z)}}, lt);
    }
    quint64 arc(Vec2 c, double r, double a0deg, double a1deg, const QString& lay = "0", const QString& lt = QString(),
                Vec3 ext = {0, 0, 1}) {
        return entity("ARC", lay, {{"center", pt(c)}, {"radius", r}, {"start_angle", deg(a0deg)}, {"end_angle", deg(a1deg)},
                                   {"extrusion", pt(ext.x, ext.y, ext.z)}}, lt);
    }
    quint64 lwpolyline(const std::vector<Vec2>& pts, const std::vector<double>& bulges, bool closed,
                       const QString& lay = "0", Vec3 ext = {0, 0, 1}) {
        QJsonArray p, b;
        for (Vec2 v : pts) p.append(QJsonArray{v.x, v.y});
        for (double v : bulges) b.append(v);
        int flag = closed ? 512 : 0;
        if (!bulges.empty()) flag |= 16;
        QJsonObject o{{"points", p}, {"flag", flag}, {"extrusion", pt(ext.x, ext.y, ext.z)}};
        if (!bulges.empty()) o.insert("bulges", b);
        return entity("LWPOLYLINE", lay, o);
    }
    quint64 polyline2d(const std::vector<Vec2>& pts, const std::vector<double>& bulges, bool closed, const QString& lay = "0") {
        std::vector<quint64> vs;
        for (size_t i = 0; i < pts.size(); ++i) {
            QJsonObject v{{"entity", "VERTEX_2D"}, {"layer", href(layerHandle(lay))}, {"entmode", 0}, {"ltype_flags", 0},
                          {"point", pt(pts[i])}, {"bulge", i < bulges.size() ? bulges[i] : 0.0}, {"flag", 0}};
            vs.push_back(add(v));
        }
        QJsonArray va;
        for (quint64 h : vs) va.append(href(h, 3));
        return entity("POLYLINE_2D", lay, {{"flag", closed ? 1 : 0}, {"vertex", va}, {"elevation", 0.0}});
    }
    quint64 text(Vec2 p, const QString& s, const QString& lay = "0") {
        return entity("TEXT", lay, {{"ins_pt", QJsonArray{p.x, p.y}}, {"text_value", s}, {"height", 0.2}});
    }
    // `extra` adds or replaces fields: "dimstyle", "eed", "user_text"...
    quint64 dimension(const QString& kind, double meas, Vec2 a, Vec2 b, const QString& lay = "0", const QJsonObject& extra = {}) {
        QJsonObject o{{"act_measurement", meas}, {"xline1_pt", pt(a)}, {"xline2_pt", pt(b)}, {"def_pt", pt(b)},
                      {"text_midpt", QJsonArray{(a.x + b.x) / 2, (a.y + b.y) / 2}}, {"user_text", ""}};
        for (auto it = extra.begin(); it != extra.end(); ++it) o.insert(it.key(), it.value());
        return entity("DIMENSION_" + kind, lay, o);
    }
    // A DIMSTYLE table record; `vars` holds DIMDEC, DIMZIN, DIMLUNIT...
    quint64 dimstyle(const QString& name, QJsonObject vars) {
        vars.insert("name", name);
        return addObject("DIMSTYLE", vars);
    }
    quint64 insert(quint64 block, Vec2 at, Vec3 scale = {1, 1, 1}, double rotDeg = 0, const QString& lay = "0",
                   const QString& lt = QString(), Vec3 ext = {0, 0, 1}) {
        return entity("INSERT", lay, {{"ins_pt", pt(at)}, {"scale", pt(scale.x, scale.y, scale.z)}, {"rotation", deg(rotDeg)},
                                      {"block_header", href(block)}, {"extrusion", pt(ext.x, ext.y, ext.z)}}, lt);
    }
    quint64 raw(const QString& type, const QString& lay, QJsonObject extra) { return entity(type, lay, extra); }

    // Block definitions: entities created between begin/end belong to the block.
    quint64 beginBlock(const QString& name, Vec3 base = {}) {
        m_blockOpen = true;
        m_blockHandle = m_next++;
        m_blockName = name;
        m_blockBase = base;
        m_blockEntities.clear();
        return m_blockHandle;
    }
    void endBlock() {
        QJsonArray ents;
        for (quint64 h : m_blockEntities) ents.append(href(h, 3));
        QJsonObject o{{"object", "BLOCK_HEADER"}, {"name", m_blockName}, {"base_pt", pt(m_blockBase.x, m_blockBase.y, m_blockBase.z)},
                      {"entities", ents}, {"anonymous", 0}, {"blkisxref", 0},
                      {"handle", QJsonArray{0, 1, static_cast<double>(m_blockHandle)}}};
        m_objects.append(o);
        m_blockOpen = false;
    }
    void setPaperSpace(bool on) { m_paper = on; }
    QJsonObject& header() { return m_header; }
    // The *Model_Space block record, for objects that point at it (a SORTENTSTABLE's owner).
    quint64 modelSpaceRecord() {
        if (!m_modelSpace)
            m_modelSpace = addObject("BLOCK_HEADER", {{"name", "*Model_Space"}, {"base_pt", pt(0, 0, 0)},
                                                      {"entities", QJsonArray{}}, {"anonymous", 0}, {"blkisxref", 0}});
        return m_modelSpace;
    }
    // Any non-entity object ("SORTENTSTABLE", ...); returns its handle.
    quint64 object(const QString& type, QJsonObject o) { return addObject(type, o); }

    QJsonObject root() const {
        return QJsonObject{{"created_by", "test"}, {"FILEHEADER", QJsonObject{{"version", "AC1032"}}}, {"HEADER", m_header},
                           {"OBJECTS", m_objects}};
    }
    dc::DwgJson json() const {
        dc::DwgJson j;
        QString err;
        j.setRoot(root(), &err);
        return j;
    }
    dc::Drawing canon(const dc::CanonSettings& s = {}) const { return dc::canonicalize(json(), s); }

    quint64 ltByBlock = 0, ltByLayer = 0, ltContinuous = 0, layer0 = 0;

private:
    quint64 add(QJsonObject o) {
        const quint64 h = m_next++;
        o.insert("handle", QJsonArray{0, 1, static_cast<double>(h)});
        m_objects.append(o);
        return h;
    }
    quint64 addObject(const QString& type, QJsonObject o) {
        o.insert("object", type);
        return add(o);
    }

    QJsonArray m_objects;
    QJsonObject m_header{{"INSUNITS", 1}};
    quint64 m_next = 0x20;
    std::vector<std::pair<QString, quint64>> m_ltypes, m_layers;
    bool m_blockOpen = false, m_paper = false;
    quint64 m_blockHandle = 0;
    quint64 m_modelSpace = 0;
    QString m_blockName;
    Vec3 m_blockBase;
    std::vector<quint64> m_blockEntities;
};

inline int countKind(const dc::Drawing& d, dc::PrimKind k) { return d.count(k); }

// A copy of a drawing with every primitive moved by t (e.g. a scaled or rotated submission).
inline std::shared_ptr<dc::Drawing> transformedCopy(const dc::Drawing& d, const dc::Transform2D& t) {
    auto out = std::make_shared<dc::Drawing>(d);
    for (auto& p : out->prims) p = dc::transformed(p, t);
    return out;
}

inline const dc::Primitive* findLine(const dc::Drawing& d, Vec2 a, Vec2 b, double tol = 1e-9) {
    for (const auto& p : d.prims)
        if (p.kind == dc::PrimKind::Line &&
            ((dc::dist(p.p0, a) <= tol && dc::dist(p.p1, b) <= tol) || (dc::dist(p.p0, b) <= tol && dc::dist(p.p1, a) <= tol)))
            return &p;
    return nullptr;
}

inline bool hasWarning(const dc::Drawing& d, dc::WarnKind k) {
    for (const auto& w : d.warnings)
        if (w.kind == k) return true;
    return false;
}

}  // namespace dct
