#pragma once
// Index over `dwgread -O JSON` output. Field names are documented, with the
// evidence for each, in FIELDMAP.md.

#include "Geometry.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include <vector>

namespace dc {

class DwgJson {
public:
    struct Layer {
        quint64 handle = 0;
        QString name;
        quint64 ltype = 0;
        int flag0 = 0;  // 1 frozen, 2 off, 8 locked, 16 plot
        int color = 7;          // ACI number, or -1 for a true color
        quint32 rgb = 0xFFFFFF; // the color as RGB
        bool frozen() const { return flag0 & 1; }
        bool off() const { return flag0 & 2; }
        bool locked() const { return flag0 & 8; }
        bool plot() const { return flag0 & 16; }
    };
    struct Block {
        quint64 handle = 0;
        QString name;
        Vec3 base;
        bool anonymous = false;
        bool xref = false;
        std::vector<quint64> entities;  // owned entities, drawing order
    };

    // Parses JSON text. Falls back to a sanitized copy (nan/inf tokens) if strict parsing fails.
    bool parse(const QByteArray& json, QString* error);
    // Build from an already-parsed root object (used by tests).
    bool setRoot(const QJsonObject& root, QString* error);

    const QJsonObject& root() const { return m_root; }
    const QJsonObject& header() const { return m_header; }
    QJsonObject fileHeader() const { return m_root.value("FILEHEADER").toObject(); }
    QJsonObject summaryInfo() const { return m_root.value("SummaryInfo").toObject(); }
    bool hasSummaryInfo() const { return m_root.contains("SummaryInfo"); }
    QJsonObject appInfo() const { return m_root.value("AppInfo").toObject(); }
    QString createdBy() const { return m_root.value("created_by").toString(); }

    const QJsonObject* object(quint64 handle) const;
    QString typeOf(const QJsonObject& o) const;  // "entity" or "object" value

    const QHash<quint64, Layer>& layers() const { return m_layers; }
    const Layer* layer(quint64 handle) const;
    const Layer* layerByName(const QString& name) const;
    QString ltypeName(quint64 handle) const;
    const Block* block(quint64 handle) const;

    // Top-level entities in model space / paper space, in file (database) order.
    const std::vector<quint64>& modelSpace() const { return m_model; }
    const std::vector<quint64>& paperSpace() const { return m_paper; }
    // The block records of model and paper space (0 when the file has none).
    quint64 modelSpaceBlock() const { return m_modelBlock; }
    quint64 paperSpaceBlock() const { return m_paperBlock; }
    // `ents` (one block's entities, or model / paper space's, in file order) in AutoCAD's
    // draw order: by their sort handle in that block's SORTENTSTABLE (what DRAWORDER and
    // hatching "behind the boundary" write), an entity it doesn't list by its own handle.
    // Without a table, the file order.
    std::vector<quint64> drawOrder(const std::vector<quint64>& ents, quint64 blockHandle) const;
    // Vertices of a POLYLINE_2D/3D (R2004+: "vertex" array; R2000: owner scan).
    std::vector<quint64> polylineVertices(const QJsonObject& pline) const;

    // Helpers for reading values.
    static quint64 href(const QJsonValue& v);    // last element of a handle array
    static quint64 ownHandle(const QJsonObject& o);
    static Vec3 vec3(const QJsonValue& v, Vec3 def = {});
    static Vec2 vec2(const QJsonValue& v, Vec2 def = {});
    static double num(const QJsonObject& o, const char* key, double def = 0);
    static int integer(const QJsonObject& o, const char* key, int def = 0);

private:
    void index();

    QJsonObject m_root;
    QJsonObject m_header;
    std::vector<QJsonObject> m_objects;
    QHash<quint64, int> m_byHandle;
    QHash<quint64, Layer> m_layers;
    QHash<QString, quint64> m_layerByName;  // upper-case name
    QHash<quint64, QString> m_ltypes;
    QHash<quint64, Block> m_blocks;
    std::vector<quint64> m_model, m_paper;
    quint64 m_modelBlock = 0, m_paperBlock = 0;
    QHash<quint64, QHash<quint64, quint64>> m_sortKeys;  // block record -> entity -> sort handle
    QHash<quint64, std::vector<quint64>> m_ownedBy;  // owner handle -> owned entity handles (file order)
};

}  // namespace dc
