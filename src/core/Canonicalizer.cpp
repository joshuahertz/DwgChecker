#include "Canonicalizer.h"

#include "Color.h"

#include "MetaExtractor.h"
#include "Dsu.h"
#include "SpatialGrid.h"

#include <QFileInfo>
#include <QMap>
#include <QSet>

#include <algorithm>
#include <numeric>

namespace dc {

// ---------------------------------------------------------------------------
// Linetype classes (rule 10)
// ---------------------------------------------------------------------------

LinetypeClassifier::LinetypeClassifier(const CanonSettings& s) {
    auto add = [this](const QStringList& pats, LtClass c) {
        for (const QString& p : pats) {
            if (p.trimmed().isEmpty()) continue;
            QRegularExpression re(QRegularExpression::wildcardToRegularExpression(p.trimmed()),
                                  QRegularExpression::CaseInsensitiveOption);
            m_rules.emplace_back(re, c);
        }
    };
    add(s.visiblePatterns, LtClass::Visible);
    add(s.hiddenPatterns, LtClass::Hidden);
    add(s.centerPatterns, LtClass::Center);
    add(s.phantomPatterns, LtClass::Phantom);
}

LtClass LinetypeClassifier::classify(const QString& name) const {
    for (const auto& [re, c] : m_rules)
        if (re.match(name).hasMatch()) return c;
    return LtClass::Other;
}

// ---------------------------------------------------------------------------
// Traversal: model space, INSERT explosion, entity conversion (rules 1-4, 8-10)
// ---------------------------------------------------------------------------

namespace {

QString hex(quint64 h) { return QString::number(h, 16).toUpper(); }

class Builder {
public:
    Builder(const DwgJson& j, const CanonSettings& s, Drawing& d) : J(j), S(s), D(d), lt(s) {
        if (!s.layerIgnoreRegex.trimmed().isEmpty())
            ignore = QRegularExpression(s.layerIgnoreRegex, QRegularExpression::CaseInsensitiveOption);
    }

    struct Ctx {
        Affine3 M;            // block coordinates -> WCS
        QString insLayer;     // effective layer of the enclosing INSERT (layer "0" inheritance)
        QString insLtype;     // effective linetype of the enclosing INSERT (ByBlock)
        int insAci = 7;                // effective color of the enclosing INSERT (ByBlock)
        quint32 insRgb = 0xFFFFFF;
        bool insColorByLayer = true;   // ... and whether the INSERT's color and linetype are by layer
        bool insLtByLayer = true;
        QString idPrefix;     // "29C/" ...
        QString typePrefix;   // "INSERT>" ...
        quint64 top = 0;      // top-level entity handle
        int depth = 0;
        int drawOrder = 0;    // the top-level entity's place in AutoCAD's draw order
    };

    void run() {
        const auto& top = S.usePaperSpace ? J.paperSpace() : J.modelSpace();
        // AutoCAD's draw order (DRAWORDER), for showing the file as AutoCAD does. The entities
        // are still read in file order, so the primitives and the grading don't depend on it.
        const std::vector<quint64> drawn = J.drawOrder(top, S.usePaperSpace ? J.paperSpaceBlock() : J.modelSpaceBlock());
        QHash<quint64, int> rank;
        for (int k = 0; k < static_cast<int>(drawn.size()); ++k) rank.insert(drawn[k], k);
        for (quint64 h : top) {
            Ctx c;
            c.top = h;
            c.drawOrder = rank.value(h);
            visit(h, c);
        }
        // Top-level handle pattern (informational duplicate signal): model space only.
        std::vector<quint64> hs = J.modelSpace();
        std::sort(hs.begin(), hs.end());
        for (quint64 h : hs) {
            const QJsonObject* o = J.object(h);
            D.entityHandles.push_back(h);
            D.entityTypes << (o ? J.typeOf(*o) : QString());
        }
        for (const auto& l : J.layers()) {
            LayerInfo li;
            li.name = l.name;
            li.ltype = J.ltypeName(l.ltype);
            li.color = l.color;
            li.rgb = l.rgb;
            li.frozen = l.frozen(); li.off = l.off(); li.locked = l.locked(); li.plot = l.plot();
            D.layers.push_back(li);
        }
        std::sort(D.layers.begin(), D.layers.end(), [](const LayerInfo& a, const LayerInfo& b) { return a.name < b.name; });
    }

    // Counters turned into warnings by finish().
    QMap<QString, int> unsupported;
    int nonUniform = 0, tilted = 0, zCount = 0, hiddenLayerCount = 0, xrefs = 0, tooDeep = 0, minsertNoCounts = 0;
    int nonFinite = 0;
    QSet<QString> hiddenLayers;
    double zMax = 0;

    void finish() {
        if (zCount)
            D.addWarning(WarnKind::NonZeroZ, QStringLiteral("%1 entities have nonzero Z (max |z| = %2); Z was ignored")
                                                 .arg(zCount).arg(zMax, 0, 'g', 6));
        if (nonUniform)
            D.addWarning(WarnKind::NonUniformScale,
                         QStringLiteral("%1 arcs/circles are inside non-uniformly scaled blocks and were marked unsupported")
                             .arg(nonUniform));
        if (tilted)
            D.addWarning(WarnKind::NonZeroZ,
                         QStringLiteral("%1 arcs/circles are not parallel to the XY plane and were marked unsupported").arg(tilted));
        D.hiddenLayerObjects = hiddenLayerCount;
        D.hiddenLayerObjectsSkipped = hiddenLayerCount > 0 && S.skipHiddenLayers;
        if (hiddenLayerCount) {
            QStringList names(hiddenLayers.begin(), hiddenLayers.end());
            names.sort(Qt::CaseInsensitive);
            const QString which = names.size() > 4 ? names.mid(0, 4).join(QStringLiteral(", ")) + QStringLiteral(", …")
                                                   : names.join(QStringLiteral(", "));
            D.addWarning(WarnKind::HiddenLayer,
                         S.skipHiddenLayers
                             ? QStringLiteral("%1 objects on turned-off or frozen layers (%2) were ignored").arg(hiddenLayerCount).arg(which)
                             : QStringLiteral("%1 objects are on turned-off or frozen layers (%2) and were included")
                                   .arg(hiddenLayerCount).arg(which));
        }
        if (nonFinite)
            D.addWarning(WarnKind::Load, QStringLiteral("%1 objects have unreadable numbers (nan or inf from the parser) and "
                                                        "were left out").arg(nonFinite));
        if (xrefs) D.addWarning(WarnKind::Xref, QStringLiteral("%1 external references were not loaded").arg(xrefs));
        if (tooDeep) D.addWarning(WarnKind::Other, QStringLiteral("%1 block references nested too deeply (recursive block?)").arg(tooDeep));
        if (minsertNoCounts)
            D.addWarning(WarnKind::Unsupported,
                         QStringLiteral("%1 MINSERT arrays could not be exploded: this dwgread build omits num_cols/num_rows "
                                        "(see third_party/patches/0001)").arg(minsertNoCounts));
    }

private:
    const DwgJson& J;
    const CanonSettings& S;
    Drawing& D;
    LinetypeClassifier lt;
    QRegularExpression ignore;

    QString layerName(const QJsonObject& o) const {
        const auto* l = J.layer(DwgJson::href(o.value("layer")));
        return l ? l->name : QStringLiteral("0");
    }
    QString effLayer(const QJsonObject& o, const Ctx& c) const {
        const QString n = layerName(o);
        if (c.depth > 0 && n == "0" && !c.insLayer.isEmpty()) return c.insLayer;
        return n;
    }
    QString layerLtype(const QString& layer) const {
        const auto* l = J.layerByName(layer);
        if (!l) return QStringLiteral("Continuous");
        const QString n = J.ltypeName(l->ltype);
        return n.isEmpty() ? QStringLiteral("Continuous") : n;
    }
    QString effLtype(const QJsonObject& o, const QString& effLayerName, const Ctx& c) const {
        const int flags = DwgJson::integer(o, "ltype_flags", 0);
        QString name;
        switch (flags) {
        case 0: name = QStringLiteral("ByLayer"); break;
        case 1: name = QStringLiteral("ByBlock"); break;
        case 2: name = QStringLiteral("Continuous"); break;
        default: name = J.ltypeName(DwgJson::href(o.value("ltype"))); break;
        }
        if (name.compare("ByLayer", Qt::CaseInsensitive) == 0 || name.isEmpty()) return layerLtype(effLayerName);
        if (name.compare("ByBlock", Qt::CaseInsensitive) == 0)
            return c.depth > 0 && !c.insLtype.isEmpty() ? c.insLtype : QStringLiteral("Continuous");
        return name;
    }
    // The object's effective color, and whether it comes from its layer. ByBlock takes the
    // enclosing INSERT's color (white at the top level, where it is not by layer).
    struct EffColor {
        int aci = 7;
        quint32 rgb = 0xFFFFFF;
        bool byLayer = true;
    };
    EffColor effColor(const QJsonObject& o, const QString& effLayerName, const Ctx& c) const {
        const DwgColor col = parseDwgColor(o.value("color"));
        EffColor e;
        switch (col.kind) {
        case DwgColor::Kind::ByLayer:
            if (const auto* l = J.layerByName(effLayerName)) {
                e.aci = l->color;
                e.rgb = l->rgb;
            }
            break;
        case DwgColor::Kind::ByBlock:
            if (c.depth > 0) {
                e.aci = c.insAci;
                e.rgb = c.insRgb;
                e.byLayer = c.insColorByLayer;
            } else {
                e.byLayer = false;
            }
            break;
        case DwgColor::Kind::Aci:
            e.aci = col.aci;
            e.rgb = aciToRgb(col.aci);
            e.byLayer = false;
            break;
        case DwgColor::Kind::Rgb:
            e.aci = -1;
            e.rgb = col.rgb;
            e.byLayer = false;
            break;
        }
        return e;
    }
    // effColor and ltByLayer for one entity in one context: the same for all its primitives
    // (a polyline's segments, a MINSERT's cells), so they are resolved once.
    struct AttrMemo {
        const QJsonObject* o = nullptr;
        QString layer;
        int depth = -1, insAci = 0;
        quint32 insRgb = 0;
        bool insColorByLayer = false, insLtByLayer = false;
        EffColor color;
        bool ltByLayer = true;
    };
    mutable AttrMemo memo;
    const AttrMemo& attrsOf(const QJsonObject& o, const QString& layer, const Ctx& c) const {
        if (memo.o != &o || memo.layer != layer || memo.depth != c.depth || memo.insAci != c.insAci ||
            memo.insRgb != c.insRgb || memo.insColorByLayer != c.insColorByLayer || memo.insLtByLayer != c.insLtByLayer)
            memo = {&o, layer, c.depth, c.insAci, c.insRgb, c.insColorByLayer, c.insLtByLayer, effColor(o, layer, c), ltByLayer(o, c)};
        return memo;
    }
    // Whether the object's linetype comes from its layer (ltype_flags 0 ByLayer, 1 ByBlock).
    bool ltByLayer(const QJsonObject& o, const Ctx& c) const {
        switch (DwgJson::integer(o, "ltype_flags", 0)) {
        case 0: return true;
        case 1: return c.depth > 0 && c.insLtByLayer;
        default: return false;
        }
    }
    bool layerSkipped(const QString& layer) const {
        if (S.skipDefpoints && layer.compare("Defpoints", Qt::CaseInsensitive) == 0) return true;
        if (ignore.isValid() && !ignore.pattern().isEmpty() && ignore.match(layer).hasMatch()) return true;
        return false;
    }
    bool layerHidden(const QString& layer) const {
        const auto* l = J.layerByName(layer);
        return l && (l->frozen() || l->off());
    }
    bool layerFrozen(const QString& layer) const {
        const auto* l = J.layerByName(layer);
        return l && l->frozen();
    }

    Primitive proto(const QJsonObject& o, const QString& type, const QString& layer, const QString& ltype,
                    const Ctx& c, int seg = -1) const {
        Primitive p;
        p.layer = layer;
        p.ltypeName = ltype;
        p.ltClass = lt.classify(ltype);
        p.drawOrder = c.drawOrder;
        const AttrMemo& a = attrsOf(o, layer, c);
        p.aci = a.color.aci;
        p.rgb = a.color.rgb;
        p.colorByLayer = a.color.byLayer;
        p.ltByLayer = a.ltByLayer;
        p.sourceType = c.typePrefix + type;
        p.sourceId = c.idPrefix + hex(DwgJson::ownHandle(o)) + (seg >= 0 ? QStringLiteral("#%1").arg(seg) : QString());
        p.handle = c.top;
        return p;
    }

    // How a dimension formats its value: its DIMSTYLE (a sub-style such as "DC3$4" is
    // referenced directly), then the per-dimension overrides that AutoCAD keeps as
    // ACAD "DSTYLE" extended data: "{", pairs of (DXF group code, value), "}".
    // See FIELDMAP.md "DIMENSION".
    void readDimFormat(Primitive& p, const QJsonObject& o) const {
        static const std::pair<int, const char*> vars[] = {{271, "DIMDEC"},  {179, "DIMADEC"},  {78, "DIMZIN"},
                                                           {79, "DIMAZIN"},  {277, "DIMLUNIT"}, {275, "DIMAUNIT"},
                                                           {278, "DIMDSEP"}, {144, "DIMLFAC"},  {45, "DIMRND"}};
        QHash<int, double> v;
        const QJsonObject* st = J.object(DwgJson::href(o.value("dimstyle")));
        const QJsonObject& src = st && J.typeOf(*st) == QLatin1String("DIMSTYLE") ? *st : J.header();
        for (const auto& [code, key] : vars)
            if (src.contains(QLatin1String(key))) v.insert(code, src.value(QLatin1String(key)).toDouble());
        const QJsonArray eed = o.value("eed").toArray();
        for (qsizetype i = 0; i < eed.size(); ++i) {
            const QJsonObject e = eed[i].toObject();
            if (e.value("code").toInt() != 0 || e.value("value").toString() != QLatin1String("DSTYLE")) continue;
            for (qsizetype k = i + 1; k < eed.size();) {
                const QJsonObject a = eed[k].toObject();
                if (a.contains("handle")) break;  // the next application's data
                if (a.value("code").toInt() == 2) {  // control string: 0 "{", 1 "}"
                    if (a.value("value").toInt() == 1) break;
                    ++k;
                    continue;
                }
                if (k + 1 >= eed.size()) break;
                v.insert(a.value("value").toInt(), eed[k + 1].toObject().value("value").toDouble());
                k += 2;
            }
            break;
        }
        if (!v.contains(271)) return;  // no style information: the precision stays unknown
        const bool angular = isAngularDimType(p.dimType);
        const int dec = static_cast<int>(v.value(271));
        const int adec = static_cast<int>(v.value(179, -1));  // -1: angles use DIMDEC
        p.dimUnit = static_cast<int>(angular ? v.value(275, 0) : v.value(277, 2));
        p.dimPrec = angular ? (adec < 0 ? dec : adec) : dec;
        p.dimZin = static_cast<int>(angular ? v.value(79, 0) : v.value(78, 0));
        p.dimLfac = angular ? 1.0 : v.value(144, 1.0);
        p.dimRnd = angular ? 0.0 : v.value(45, 0.0);
        const int sep = static_cast<int>(v.value(278, '.'));
        p.dimSep = sep > 32 && sep < 127 ? QChar(sep) : QLatin1Char('.');
    }

    void noteZ(double z) {
        if (std::fabs(z) > S.linTol) {
            ++zCount;
            zMax = std::max(zMax, std::fabs(z));
        }
    }

    void emitLine(Primitive p, Vec3 a, Vec3 b) {
        noteZ(a.z); noteZ(b.z);
        p.kind = PrimKind::Line;
        p.p0 = a.xy();
        p.p1 = b.xy();
        D.prims.push_back(std::move(p));
    }

    // L maps the entity's OCS to WCS (block transform included).
    void emitArc(Primitive p, const Affine3& L, Vec3 cOcs, double r, double a0, double a1, bool full) {
        const Vec3 c = L.apply(cOcs);
        const Vec3 ex = L.applyLinear({1, 0, 0}), ey = L.applyLinear({0, 1, 0});
        const Vec2 c0 = ex.xy(), c1 = ey.xy();
        const double s0 = c0.length(), s1 = c1.length();
        p.center = c.xy();
        if (std::fabs(ex.z) > 1e-9 * std::max(1.0, ex.length()) || std::fabs(ey.z) > 1e-9 * std::max(1.0, ey.length())) {
            ++tilted;
            p.kind = PrimKind::Unsupported;
            p.sourceType += QStringLiteral(" (tilted)");
            D.prims.push_back(std::move(p));
            return;
        }
        if (std::fabs(s0 - s1) > 1e-9 * std::max(s0, s1) || std::fabs(dot(c0, c1)) > 1e-9 * s0 * s1) {
            ++nonUniform;
            p.kind = PrimKind::Unsupported;
            p.sourceType += QStringLiteral(" (non-uniform scale)");
            D.prims.push_back(std::move(p));
            return;
        }
        noteZ(c.z);
        p.radius = r * s0;
        if (full) {
            p.kind = PrimKind::Circle;
        } else {
            const double phi = std::atan2(c0.y, c0.x);
            p.kind = PrimKind::Arc;
            if (cross(c0, c1) > 0) { p.a0 = a0 + phi; p.a1 = a1 + phi; }
            else { p.a0 = phi - a1; p.a1 = phi - a0; }  // mirrored: orientation reverses (extrusion -Z, mirrored INSERT)
            p.a0 = normalizeAngle(p.a0);
            p.a1 = normalizeAngle(p.a1);
        }
        D.prims.push_back(std::move(p));
    }

    void emitUnsupported(Primitive p, const QJsonObject& o, const Affine3& M) {
        p.kind = PrimKind::Unsupported;
        Vec3 pos;
        for (const char* k : {"center", "point", "ins_pt", "start"})
            if (o.contains(QLatin1StringView(k))) { pos = DwgJson::vec3(o.value(QLatin1StringView(k))); break; }
        if (o.contains("fit_pts")) pos = DwgJson::vec3(o.value("fit_pts").toArray().at(0));
        else if (o.contains("ctrl_pts")) pos = DwgJson::vec3(o.value("ctrl_pts").toArray().at(0));
        p.center = M.apply(pos).xy();
        unsupported[p.sourceType]++;
        D.prims.push_back(std::move(p));
    }

    void visit(quint64 h, const Ctx& c) {
        const QJsonObject* op = J.object(h);
        if (!op) return;
        const QJsonObject& o = *op;
        const QString type = J.typeOf(o);
        static const QStringList ignored = {
            "MPOLYGON", "SOLID", "TRACE", "POINT", "WIPEOUT", "VIEWPORT", "LEADER", "MULTILEADER", "MLEADER",
            "TOLERANCE", "IMAGE", "OLE2FRAME", "OLEFRAME", "ATTDEF", "SEQEND", "BLOCK", "ENDBLK", "PDFUNDERLAY",
            "DWFUNDERLAY", "DGNUNDERLAY", "LIGHT", "CAMERA", "SUN", "GEOPOSITIONMARKER", "VERTEX_2D", "VERTEX_3D",
            "VERTEX_MESH", "VERTEX_PFACE", "VERTEX_PFACE_FACE", "PROXY_ENTITY", "SECTIONOBJECT"};
        if (ignored.contains(type)) return;
        if (J.hadNonFinite() && DwgJson::hasNull(o)) {  // a nan read as null (DwgJson::parse)
            ++nonFinite;
            return;
        }

        const QString layer = effLayer(o, c);
        if (layerSkipped(layer)) return;
        // A block on a turned-off layer still shows its objects that are on other layers, as in
        // AutoCAD; those on layer 0 take the block's layer and are hidden with it (effLayer). A
        // frozen layer hides the whole block (D76).
        const bool block = type == "INSERT" || type == "MINSERT";
        if (block ? layerFrozen(layer) : layerHidden(layer)) {
            ++hiddenLayerCount;
            hiddenLayers.insert(layer);
            if (S.skipHiddenLayers) return;
        }
        const QString ltype = effLtype(o, layer, c);
        const Vec3 ext = DwgJson::vec3(o.value("extrusion"), {0, 0, 1});

        if (type == "LINE") {
            emitLine(proto(o, type, layer, ltype, c), c.M.apply(DwgJson::vec3(o.value("start"))),
                     c.M.apply(DwgJson::vec3(o.value("end"))));
        } else if (type == "ARC" || type == "CIRCLE") {
            const Affine3 L = c.M * Affine3::ocsToWcs(ext);
            emitArc(proto(o, type, layer, ltype, c), L, DwgJson::vec3(o.value("center")), DwgJson::num(o, "radius"),
                    DwgJson::num(o, "start_angle"), DwgJson::num(o, "end_angle"), type == "CIRCLE");
        } else if (type == "LWPOLYLINE") {
            const Affine3 L = c.M * Affine3::ocsToWcs(ext);
            const double elev = DwgJson::num(o, "elevation", 0);
            const QJsonArray pts = o.value("points").toArray();
            const QJsonArray bul = o.value("bulges").toArray();
            const int flag = DwgJson::integer(o, "flag", 0);
            const bool closed = (flag & 512) != 0;
            std::vector<Vec2> v;
            for (const auto& pv : pts) v.push_back(DwgJson::vec2(pv));
            std::vector<double> b(v.size(), 0.0);
            for (int i = 0; i < std::min<int>(bul.size(), static_cast<int>(v.size())); ++i) b[i] = bul.at(i).toDouble();
            emitPolyline(o, type, layer, ltype, c, L, v, b, elev, closed);
        } else if (type == "POLYLINE_2D") {
            const int flag = DwgJson::integer(o, "flag", 0);
            if (flag & 4) {  // spline-fit: a curve, not lines and arcs
                emitUnsupported(proto(o, type + QStringLiteral(" (spline-fit)"), layer, ltype, c), o, c.M);
                return;
            }
            const Affine3 L = c.M * Affine3::ocsToWcs(ext);
            double elev = DwgJson::num(o, "elevation", 0);
            std::vector<Vec2> v;
            std::vector<double> b;
            for (quint64 vh : J.polylineVertices(o)) {
                const QJsonObject* vo = J.object(vh);
                if (!vo) continue;
                if (DwgJson::integer(*vo, "flag", 0) & 16) continue;  // spline frame control point
                const Vec3 p = DwgJson::vec3(vo->value("point"));
                if (elev == 0 && p.z != 0) elev = p.z;
                v.push_back(p.xy());
                b.push_back(DwgJson::num(*vo, "bulge", 0));
            }
            emitPolyline(o, type, layer, ltype, c, L, v, b, elev, (flag & 1) != 0);
        } else if (type == "POLYLINE_3D") {
            const int flag = DwgJson::integer(o, "flag", 0);
            if (flag & 4) {
                emitUnsupported(proto(o, type + QStringLiteral(" (spline-fit)"), layer, ltype, c), o, c.M);
                return;
            }
            std::vector<Vec3> v;
            for (quint64 vh : J.polylineVertices(o)) {
                const QJsonObject* vo = J.object(vh);
                if (!vo) continue;
                if (DwgJson::integer(*vo, "flag", 0) & 16) continue;
                v.push_back(c.M.apply(DwgJson::vec3(vo->value("point"))));
            }
            const bool closed = (flag & 1) != 0;
            const int n = static_cast<int>(v.size());
            for (int i = 0; i + 1 < n + (closed ? 1 : 0); ++i)
                emitLine(proto(o, type, layer, ltype, c, i), v[i], v[(i + 1) % n]);
        } else if (type == "HATCH") {
            emitHatch(o, type, layer, ltype, c, ext);
        } else if (type == "INSERT" || type == "MINSERT") {
            explode(o, type, layer, ltype, c, ext);
        } else if (type == "TEXT" || type == "ATTRIB") {
            Primitive p = proto(o, type, layer, ltype, c);
            p.kind = PrimKind::Text;
            const Vec2 ip = DwgJson::vec2(o.value("ins_pt"));
            const Affine3 L = c.M * Affine3::ocsToWcs(ext);
            p.center = L.apply({ip.x, ip.y, DwgJson::num(o, "elevation", 0)}).xy();
            p.text = o.value("text_value").toString();
            D.prims.push_back(std::move(p));
        } else if (type == "MTEXT") {
            Primitive p = proto(o, type, layer, ltype, c);
            p.kind = PrimKind::Text;
            p.center = c.M.apply(DwgJson::vec3(o.value("ins_pt"))).xy();
            p.text = o.value("text").toString();
            D.prims.push_back(std::move(p));
        } else if (type.startsWith("DIMENSION_") || type == "ARC_DIMENSION" || type == "LARGE_RADIAL_DIMENSION") {
            Primitive p = proto(o, type, layer, ltype, c);
            p.kind = PrimKind::Dimension;
            p.dimType = type.startsWith("DIMENSION_") ? type.mid(10) : type;
            p.measurement = DwgJson::num(o, "act_measurement", 0);
            p.text = o.value("user_text").toString();
            readDimFormat(p, o);
            const Affine3 L = c.M * Affine3::ocsToWcs(ext);
            const Vec2 tm = DwgJson::vec2(o.value("text_midpt"));
            p.center = L.apply({tm.x, tm.y, DwgJson::num(o, "elevation", 0)}).xy();
            if (o.contains("xline1_pt")) {
                p.p0 = c.M.apply(DwgJson::vec3(o.value("xline1_pt"))).xy();
                p.p1 = c.M.apply(DwgJson::vec3(o.value("xline2_pt"))).xy();
            } else {
                p.p0 = c.M.apply(DwgJson::vec3(o.value("def_pt"))).xy();
                p.p1 = o.contains("first_arc_pt") ? c.M.apply(DwgJson::vec3(o.value("first_arc_pt"))).xy() : p.p0;
            }
            // Where it measures, for matching (Matcher's dimensionPlaceMatches).
            auto at = [&](const char* key) { return c.M.apply(DwgJson::vec3(o.value(QLatin1StringView(key)))).xy(); };
            if (o.contains("feature_location_pt")) {  // ORDINATE: the feature, then the leader's end
                p.p0 = at("feature_location_pt");
                p.p1 = o.contains("leader_endpt") ? at("leader_endpt") : p.p0;
            }
            if (o.contains("center_pt")) {  // ANG3PT, ARC_DIMENSION
                p.dimCenter = at("center_pt");
                p.hasDimCenter = true;
            } else if (p.dimType == "RADIUS" || p.dimType == "LARGE_RADIAL_DIMENSION") {  // def_pt is the center
                p.dimCenter = at("def_pt");
                p.hasDimCenter = true;
            } else if (p.dimType == "DIAMETER" && o.contains("first_arc_pt")) {  // two opposite points on the circle
                p.dimCenter = (at("def_pt") + at("first_arc_pt")) * 0.5;
                p.hasDimCenter = true;
            } else if (o.contains("xline1start_pt") && o.contains("xline2start_pt")) {  // ANG2LN: where the lines meet
                const Vec2 a = at("xline1start_pt"), b = at("xline2start_pt");
                const Vec2 da = at("xline1end_pt") - a, db = at("xline2end_pt") - b;
                const double den = cross(da, db);
                if (std::fabs(den) > 1e-12 * da.length() * db.length()) {
                    p.dimCenter = a + da * (cross(b - a, db) / den);
                    p.hasDimCenter = true;
                }
            }
            // What it measures between (D78): a linear one's direction (its rotation; an aligned
            // one's from its points), an angular one's two sides. The edges come in postProcess,
            // once the lines are merged.
            auto place = std::make_shared<DimPlace>();
            bool placed = false;
            if (p.dimType == "LINEAR") {
                const double rot = DwgJson::num(o, "dim_rotation", 0);
                place->dir = L.apply({std::cos(rot), std::sin(rot), 0}).xy() - L.apply({0, 0, 0}).xy();
                placed = true;
            } else if (p.dimType == "ALIGNED") {
                place->dir = p.p1 - p.p0;
                placed = place->dir.length() > 0;
            } else if (p.dimType == "ANG3PT" && p.hasDimCenter) {
                place->side0 = p.p0 - p.dimCenter;
                place->side1 = p.p1 - p.dimCenter;
                placed = true;
            } else if (p.dimType == "ANG2LN" && o.contains("xline1end_pt") && o.contains("xline2end_pt")) {
                place->side0 = at("xline1end_pt") - at("xline1start_pt");
                place->side1 = at("xline2end_pt") - at("xline2start_pt");
                place->sidesAreLines = true;
                placed = true;
            }
            if (placed) p.dimPlace = std::move(place);
            readDimLook(o, c, p);
            D.prims.push_back(std::move(p));
        } else {
            // SPLINE, ELLIPSE, XLINE, RAY, REGION, 3DSOLID, MLINE, 3DFACE, meshes...: reported, never compared (rule 8).
            emitUnsupported(proto(o, type, layer, ltype, c), o, c.M);
        }
    }

    // How a dimension is drawn (D87): the lines, arcs and filled arrowheads of its *D block,
    // in the dimension's own coordinates, each in its effective color (ByBlock is the
    // dimension's). Its text and definition points are left out: the label is drawn instead.
    void readDimLook(const QJsonObject& o, const Ctx& c, Primitive& p) const {
        const DwgJson::Block* blk = J.block(DwgJson::href(o.value("block")));
        if (!blk) return;
        Ctx n;
        n.M = c.M * Affine3::translation(blk->base * -1.0);
        n.insLayer = p.layer;
        n.insLtype = p.ltypeName;
        n.insAci = p.aci;
        n.insRgb = p.rgb;
        n.insColorByLayer = p.colorByLayer;
        n.insLtByLayer = p.ltByLayer;
        n.top = c.top;
        n.depth = c.depth + 1;
        auto look = std::make_shared<DimLook>();
        lookOf(blk->entities, n, *look);
        if (!look->shapes.empty()) p.dimLook = std::move(look);
    }

    void lookOf(const std::vector<quint64>& entities, const Ctx& c, DimLook& look) const {
        for (const quint64 h : entities) {
            const QJsonObject* op = J.object(h);
            if (!op) continue;
            const QJsonObject& o = *op;
            const QString type = J.typeOf(o);
            const QString layer = effLayer(o, c);
            const EffColor col = effColor(o, layer, c);
            const Vec3 ext = DwgJson::vec3(o.value("extrusion"), {0, 0, 1});
            const Affine3 L = c.M * Affine3::ocsToWcs(ext);
            DimShape s;
            s.aci = col.aci;
            s.rgb = col.rgb;
            if (type == "LINE") {
                s.pts = {c.M.apply(DwgJson::vec3(o.value("start"))).xy(), c.M.apply(DwgJson::vec3(o.value("end"))).xy()};
            } else if (type == "ARC" || type == "CIRCLE") {
                const Vec3 ctr = DwgJson::vec3(o.value("center"));
                const double r = DwgJson::num(o, "radius");
                const double a0 = type == "CIRCLE" ? 0.0 : DwgJson::num(o, "start_angle");
                double sweep = type == "CIRCLE" ? kTwoPi : DwgJson::num(o, "end_angle") - a0;
                while (sweep <= 0) sweep += kTwoPi;
                const int steps = arcSteps(sweep);
                for (int k = 0; k <= steps; ++k) {
                    const double a = a0 + sweep * k / steps;
                    s.pts.push_back(L.apply({ctr.x + r * std::cos(a), ctr.y + r * std::sin(a), ctr.z}).xy());
                }
            } else if (type == "SOLID") {  // corners in the order 1, 2, 4, 3; a triangle repeats the last
                const double elev = DwgJson::num(o, "elevation", 0);
                for (const char* k : {"corner1", "corner2", "corner4", "corner3"}) {
                    const Vec2 q = DwgJson::vec2(o.value(QLatin1StringView(k)));
                    const Vec2 w = L.apply({q.x, q.y, elev}).xy();
                    if (s.pts.empty() || dist(s.pts.back(), w) > 1e-12) s.pts.push_back(w);
                }
                if (s.pts.size() > 1 && dist(s.pts.front(), s.pts.back()) <= 1e-12) s.pts.pop_back();
                s.fill = s.pts.size() >= 3;
            } else if (type == "LWPOLYLINE") {
                const double elev = DwgJson::num(o, "elevation", 0);
                const QJsonArray pts = o.value("points").toArray();
                const QJsonArray bul = o.value("bulges").toArray();
                const bool closed = (DwgJson::integer(o, "flag", 0) & 512) != 0;
                std::vector<Vec2> v;
                for (const auto& pv : pts) v.push_back(DwgJson::vec2(pv));
                const int nv = static_cast<int>(v.size());
                std::vector<Vec2> flat;
                for (int i = 0; i < nv; ++i) {
                    flat.push_back(v[i]);
                    if (i + 1 < nv || closed) flattenBulge(flat, v[i], v[(i + 1) % nv], i < bul.size() ? bul.at(i).toDouble() : 0.0);
                }
                if (closed && nv > 0) flat.push_back(v.front());
                for (Vec2 q : flat) s.pts.push_back(L.apply({q.x, q.y, elev}).xy());
            } else if (type == "INSERT" && c.depth < 8) {  // an arrowhead block (_ArchTick, _Dot, ...)
                const DwgJson::Block* b = J.block(DwgJson::href(o.value("block_header")));
                if (!b) continue;
                const Vec3 ins = DwgJson::vec3(o.value("ins_pt"));
                const Vec3 sc = DwgJson::vec3(o.value("scale"), {1, 1, 1});
                Ctx n;
                n.M = c.M * Affine3::ocsToWcs(ext) * Affine3::translation(ins) * Affine3::rotationZ(DwgJson::num(o, "rotation", 0)) *
                      Affine3::scale(sc.x, sc.y, sc.z) * Affine3::translation(b->base * -1.0);
                n.insLayer = layer;
                n.insAci = col.aci;
                n.insRgb = col.rgb;
                n.insColorByLayer = col.byLayer;
                n.top = c.top;
                n.depth = c.depth + 1;
                lookOf(b->entities, n, look);
                continue;
            } else {
                continue;  // MTEXT (the label is drawn instead), POINT (definition points), ...
            }
            if (s.pts.size() >= 2) look.shapes.push_back(std::move(s));
        }
    }

    // HATCH: the filled region as flattened boundary loops (see FIELDMAP.md "HATCH").
    void emitHatch(const QJsonObject& o, const QString& type, const QString& layer, const QString& ltype, const Ctx& c, Vec3 ext) {
        const Affine3 L = c.M * Affine3::ocsToWcs(ext);
        const double elev = DwgJson::num(o, "elevation", 0);
        const int style = DwgJson::integer(o, "style", 0);  // 0 normal, 1 outer, 2 ignore islands
        auto loops = std::make_shared<std::vector<std::vector<Vec2>>>();
        for (const auto& pv : o.value("paths").toArray()) {
            const QJsonObject path = pv.toObject();
            const int flag = DwgJson::integer(path, "flag", 0);
            if (style == 2 && !(flag & 1)) continue;  // "ignore" style fills the external boundary only
            std::vector<Vec2> pts;
            if (path.contains("polyline_paths")) {
                const QJsonArray pp = path.value("polyline_paths").toArray();
                const int n = static_cast<int>(pp.size());
                for (int i = 0; i < n; ++i) {
                    const QJsonObject a = pp.at(i).toObject();
                    const Vec2 p0 = DwgJson::vec2(a.value("point"));
                    const Vec2 p1 = DwgJson::vec2(pp.at((i + 1) % n).toObject().value("point"));
                    pts.push_back(p0);
                    flattenBulge(pts, p0, p1, DwgJson::num(a, "bulge", 0));
                }
            } else {
                for (const auto& sv : path.value("segs").toArray()) flattenEdge(pts, sv.toObject());
            }
            // Drop repeated points; the loop is implicitly closed (draw-boundary paths omit the closing edge).
            std::vector<Vec2> clean;
            for (Vec2 q : pts) {
                const Vec2 w = L.apply({q.x, q.y, elev}).xy();
                if (clean.empty() || dist(clean.back(), w) > 1e-12) clean.push_back(w);
            }
            while (clean.size() > 1 && dist(clean.front(), clean.back()) <= 1e-12) clean.pop_back();
            if (clean.size() >= 3) loops->push_back(std::move(clean));
        }
        if (loops->empty()) return;
        if (style == 1 && loops->size() > 2) {
            // "Outer" fills from the outer boundary to the first island only: loops nested two or
            // more deep (an island inside an island) are dropped, then the parity rule is right (D76).
            auto inside = [](const std::vector<Vec2>& l, Vec2 q) {
                bool in = false;
                for (size_t i = 0, j = l.size() - 1; i < l.size(); j = i++)
                    if ((l[i].y > q.y) != (l[j].y > q.y) && q.x < (l[j].x - l[i].x) * (q.y - l[i].y) / (l[j].y - l[i].y) + l[i].x)
                        in = !in;
                return in;
            };
            std::vector<std::vector<Vec2>> kept;
            for (size_t a = 0; a < loops->size(); ++a) {
                int depth = 0;
                for (size_t b = 0; b < loops->size(); ++b) depth += b != a && inside((*loops)[b], (*loops)[a].front());
                if (depth <= 1) kept.push_back((*loops)[a]);
            }
            *loops = std::move(kept);
        }
        Primitive p = proto(o, type, layer, ltype, c);
        p.kind = PrimKind::Hatch;
        p.ltClass = LtClass::Visible;
        p.text = hatchPattern(o);
        p.evenOdd = style != 2;
        p.loops = loops;
        p.center = p.bbox().center();
        D.prims.push_back(std::move(p));
    }

    static QString hatchPattern(const QJsonObject& o) {
        if (DwgJson::integer(o, "is_gradient_fill", 0)) return QStringLiteral("GRADIENT ") + o.value("gradient_name").toString().toUpper();
        if (DwgJson::integer(o, "is_solid_fill", 0)) return QStringLiteral("SOLID");
        QString n = o.value("name").toString().trimmed().toUpper();
        while (n.startsWith('_')) n.remove(0, 1);  // "_SOLID" when created with the "_Solid" option
        return n.isEmpty() ? QStringLiteral("USER") : n;
    }

    // Segments for an arc: one per pi/64 of sweep. A sweep on a multiple of pi/64 (a quarter
    // circle is 32) must not tip to the next count on the last digit of the file's angles, or
    // the same drawing flattens differently depending on how precisely dwgread prints it.
    static int arcSteps(double sweep) {
        return std::max(2, static_cast<int>(std::ceil(std::fabs(sweep) / (kPi / 64) - 1e-6)));
    }

    // Intermediate points of a bulge segment p0 -> p1 (endpoints excluded), in path order.
    static void flattenBulge(std::vector<Vec2>& pts, Vec2 p0, Vec2 p1, double bulge) {
        BulgeArc arc;
        if (!bulgeToArc(p0, p1, bulge, &arc)) return;
        const double sw = 4.0 * std::atan(bulge);  // signed: negative = clockwise
        const double t0 = (p0 - arc.center).angle();
        const int n = arcSteps(sw);
        for (int k = 1; k < n; ++k) pts.push_back(arc.center + fromAngle(t0 + sw * k / n) * arc.radius);
    }

    // One edge of an edge-type boundary path. Clockwise arcs store negated angles
    // (verified on hatch_edges_cw: notch stored 2pi -> 3pi, is_ccw 0).
    static void flattenEdge(std::vector<Vec2>& pts, const QJsonObject& s) {
        const int type = DwgJson::integer(s, "curve_type", 0);
        if (type == 1) {
            pts.push_back(DwgJson::vec2(s.value("first_endpoint")));
            pts.push_back(DwgJson::vec2(s.value("second_endpoint")));
            return;
        }
        if (type == 2 || type == 3) {
            const Vec2 c = DwgJson::vec2(s.value("center"));
            const double a0 = DwgJson::num(s, "start_angle"), a1 = DwgJson::num(s, "end_angle");
            const bool ccw = DwgJson::integer(s, "is_ccw", 1) != 0;
            double sweep = a1 - a0;
            while (sweep <= 1e-9) sweep += kTwoPi;  // start == end (to rounding): a full turn
            sweep = std::min(sweep, kTwoPi);
            Vec2 major, minor;
            if (type == 2) {
                const double r = DwgJson::num(s, "radius");
                major = {r, 0};
                minor = {0, r};
            } else {
                major = DwgJson::vec2(s.value("endpoint"));
                minor = major.perp() * DwgJson::num(s, "minor_major_ratio", 1);
            }
            const int n = arcSteps(sweep);
            for (int k = 0; k <= n; ++k) {
                const double t = (a0 + sweep * k / n) * (ccw ? 1.0 : -1.0);
                pts.push_back(c + major * std::cos(t) + minor * std::sin(t));
            }
            return;
        }
        if (type == 4) {  // spline edge: its fit points, else its control polygon
            QJsonArray ptsArr = s.value("fitpts").toArray();
            if (ptsArr.isEmpty()) {
                for (const auto& cp : s.value("control_points").toArray()) pts.push_back(DwgJson::vec2(cp.toObject().value("point")));
                return;
            }
            for (const auto& fp : ptsArr) pts.push_back(DwgJson::vec2(fp));
        }
    }

    void emitPolyline(const QJsonObject& o, const QString& type, const QString& layer, const QString& ltype, const Ctx& c,
                      const Affine3& L, const std::vector<Vec2>& v, const std::vector<double>& b, double elev, bool closed) {
        const int n = static_cast<int>(v.size());
        if (n < 2) return;
        const int segs = closed ? n : n - 1;
        for (int i = 0; i < segs; ++i) {
            const Vec2 a = v[i], e = v[(i + 1) % n];
            if (closed && i == n - 1 && a == e) continue;  // explicit closing vertex
            BulgeArc arc;
            if (bulgeToArc(a, e, b[i], &arc)) {
                emitArc(proto(o, type, layer, ltype, c, i), L, {arc.center.x, arc.center.y, elev}, arc.radius,
                        arc.startAngle, arc.endAngle, false);
            } else {
                emitLine(proto(o, type, layer, ltype, c, i), L.apply({a.x, a.y, elev}), L.apply({e.x, e.y, elev}));
            }
        }
    }

    void explode(const QJsonObject& o, const QString& type, const QString& layer, const QString& ltype, const Ctx& c, Vec3 ext) {
        const DwgJson::Block* blk = J.block(DwgJson::href(o.value("block_header")));
        if (!blk) return;
        if (blk->xref) { ++xrefs; return; }
        if (c.depth >= 32) { ++tooDeep; return; }

        int cols = 1, rows = 1;
        double colSp = 0, rowSp = 0;
        if (type == "MINSERT") {
            if (!o.contains("num_cols") || !o.contains("num_rows")) {
                ++minsertNoCounts;
                emitUnsupported(proto(o, type, layer, ltype, c), o, c.M);
                return;
            }
            cols = std::max(1, DwgJson::integer(o, "num_cols", 1));
            rows = std::max(1, DwgJson::integer(o, "num_rows", 1));
            colSp = DwgJson::num(o, "col_spacing");
            rowSp = DwgJson::num(o, "row_spacing");
        }
        const Vec3 ins = DwgJson::vec3(o.value("ins_pt"));
        const Vec3 sc = DwgJson::vec3(o.value("scale"), {1, 1, 1});
        const double rot = DwgJson::num(o, "rotation", 0);
        const Affine3 head = Affine3::ocsToWcs(ext) * Affine3::translation(ins) * Affine3::rotationZ(rot);
        const Affine3 tail = Affine3::scale(sc.x, sc.y, sc.z) * Affine3::translation(blk->base * -1.0);

        const AttrMemo insAttrs = attrsOf(o, layer, c);  // a copy: visiting the block reuses the memo
        for (int r = 0; r < rows; ++r)
            for (int k = 0; k < cols; ++k) {
                Ctx n;
                n.M = c.M * head * Affine3::translation({k * colSp, r * rowSp, 0}) * tail;
                n.insLayer = layer;
                n.insLtype = ltype;
                n.insAci = insAttrs.color.aci;
                n.insRgb = insAttrs.color.rgb;
                n.insColorByLayer = insAttrs.color.byLayer;
                n.insLtByLayer = insAttrs.ltByLayer;
                n.idPrefix = c.idPrefix + hex(DwgJson::ownHandle(o)) +
                             (rows * cols > 1 ? QStringLiteral("[%1,%2]").arg(r).arg(k) : QString()) + "/";
                n.typePrefix = c.typePrefix + type + ">";
                n.top = c.top;
                n.depth = c.depth + 1;
                for (quint64 e : blk->entities) visit(e, n);
            }
        // Attributes live in the INSERT's own (outer) coordinate system.
        for (const auto& av : o.value("attribs").toArray()) visit(DwgJson::href(av), c);
    }
};

void compact(std::vector<Primitive>& prims, const std::vector<char>& removed) {
    std::vector<Primitive> out;
    out.reserve(prims.size());
    for (size_t i = 0; i < prims.size(); ++i)
        if (!removed[i]) out.push_back(std::move(prims[i]));
    prims.swap(out);
}

QString groupKey(const Primitive& p, const CanonSettings& s) {
    QString k = QString::number(static_cast<int>(p.ltClass));
    if (s.mergeByLayer) k += "|" + p.layer.toUpper();
    return k;
}

// When colors must match, pieces merge only when their colors are within the color tolerance,
// as the comparison judges them (D76); an exact RGB key split a segmented edge in near colors.
bool colorsMerge(const Primitive& a, const Primitive& b, const CanonSettings& s) {
    return !s.mergeByColor || colorDifference(a.aci, a.rgb, b.aci, b.rgb) <= s.colorTolerance + 1e-9;
}

// Pieces joined into one primitive (prims[g[i]]): when colors must match, two sets join only when
// every piece's color is within the tolerance of every other's. Checking just the pair being
// joined would chain colors drifting past it (0, 4 and 8 % apart with a tolerance of 5; D76).
struct PieceSets {
    PieceSets(int n, const std::vector<Primitive>& prims, const std::vector<int>& g, const CanonSettings& s)
        : dsu(n), members(n), prims(prims), g(g), s(s) {
        for (int i = 0; i < n; ++i) members[i] = {i};
    }
    int find(int x) { return dsu.find(x); }
    void unite(int a, int b) {
        a = dsu.find(a);
        b = dsu.find(b);
        if (a == b) return;
        if (s.mergeByColor)
            for (int x : members[a])
                for (int y : members[b])
                    if (!colorsMerge(prims[g[x]], prims[g[y]], s)) return;
        dsu.unite(a, b);
        const int root = dsu.find(a), other = root == a ? b : a;
        members[root].insert(members[root].end(), members[other].begin(), members[other].end());
        members[other].clear();
    }

    Dsu dsu;
    std::vector<std::vector<int>> members;
    const std::vector<Primitive>& prims;
    const std::vector<int>& g;
    const CanonSettings& s;
};

}  // namespace

// ---------------------------------------------------------------------------
// Post-processing passes (rules 5-7)
// ---------------------------------------------------------------------------

void normalizeArcs(std::vector<Primitive>& prims, const CanonSettings& s) {
    for (auto& p : prims) {
        if (p.kind != PrimKind::Arc) continue;
        p.a0 = normalizeAngle(p.a0);
        p.a1 = normalizeAngle(p.a1);
        const double sw = ccwSweep(p.a0, p.a1);
        // A 360-degree sweep (start == end, or within tolerance of a full turn) is a circle.
        if (sw == 0 || sw >= kTwoPi - s.angTol || (kTwoPi - sw) * p.radius <= s.linTol) {
            p.kind = PrimKind::Circle;
            p.a0 = p.a1 = 0;
        }
    }
}

void dropDegenerate(std::vector<Primitive>& prims, const CanonSettings& s) {
    std::vector<char> rm(prims.size(), 0);
    for (size_t i = 0; i < prims.size(); ++i) {
        const auto& p = prims[i];
        if (p.kind == PrimKind::Line && p.length() <= s.linTol) rm[i] = 1;
        else if ((p.kind == PrimKind::Circle || p.kind == PrimKind::Arc) && p.radius <= s.linTol) rm[i] = 1;
        else if (p.kind == PrimKind::Arc && ccwSweep(p.a0, p.a1) * p.radius <= s.linTol) rm[i] = 1;
    }
    compact(prims, rm);
}

static bool sameGeometry(const Primitive& a, const Primitive& b, double tol) {
    if (a.kind != b.kind) return false;
    switch (a.kind) {
    case PrimKind::Line:
        return (dist(a.p0, b.p0) <= tol && dist(a.p1, b.p1) <= tol) || (dist(a.p0, b.p1) <= tol && dist(a.p1, b.p0) <= tol);
    case PrimKind::Circle:
        return dist(a.center, b.center) <= tol && std::fabs(a.radius - b.radius) <= tol;
    case PrimKind::Arc:
        return dist(a.center, b.center) <= tol && std::fabs(a.radius - b.radius) <= tol &&
               dist(a.arcStart(), b.arcStart()) <= tol && dist(a.arcEnd(), b.arcEnd()) <= tol;
    default: return false;
    }
}

void removeStackedDuplicates(std::vector<Primitive>& prims, const CanonSettings& s) {
    const double tol = s.linTol;
    SpatialGrid grid(4 * tol);
    std::vector<char> rm(prims.size(), 0);
    for (int i = 0; i < static_cast<int>(prims.size()); ++i) {
        Primitive& p = prims[i];
        if (!isGeometric(p.kind)) continue;
        const Vec2 key = keyPoint(p);
        int dupOf = -1;
        grid.forNear(key, [&](int j) {
            if (dupOf < 0 && prims[j].ltClass == p.ltClass && (!s.mergeByLayer || prims[j].layer == p.layer) &&
                colorsMerge(prims[j], p, s) && sameGeometry(prims[j], p, tol))
                dupOf = j;
        });
        if (dupOf >= 0) {
            rm[i] = 1;
            prims[dupOf].stacked += 1 + p.stacked;
            prims[dupOf].mergedFrom << p.sourceId;
            prims[dupOf].drawOrder = std::max(prims[dupOf].drawOrder, p.drawOrder);  // drawn as its topmost piece
        } else {
            grid.insert(key, i);
        }
    }
    compact(prims, rm);
}

void mergeCollinearLines(std::vector<Primitive>& prims, const CanonSettings& s) {
    const double tol = s.linTol;
    QHash<QString, std::vector<int>> groups;
    for (int i = 0; i < static_cast<int>(prims.size()); ++i)
        if (prims[i].kind == PrimKind::Line) groups[groupKey(prims[i], s)].push_back(i);

    std::vector<char> rm(prims.size(), 0);
    for (auto it = groups.begin(); it != groups.end(); ++it) {
        const std::vector<int>& g = it.value();
        const int n = static_cast<int>(g.size());
        if (n < 2) continue;
        std::vector<BBox> bb(n);
        for (int i = 0; i < n; ++i) bb[i] = prims[g[i]].bbox();
        // Sort by bbox x0 so the pair scan can stop early.
        std::vector<int> order(n);
        std::iota(order.begin(), order.end(), 0);
        std::sort(order.begin(), order.end(), [&](int a, int b) { return bb[a].x0 < bb[b].x0; });
        PieceSets dsu(n, prims, g, s);
        for (int oi = 0; oi < n; ++oi) {
            const int i = order[oi];
            const Primitive& a = prims[g[i]];
            const Vec2 d = (a.p1 - a.p0).normalized();
            for (int oj = oi + 1; oj < n; ++oj) {
                const int j = order[oj];
                if (bb[j].x0 > bb[i].x1 + tol) break;
                if (bb[i].gap(bb[j]) > tol) continue;
                const Primitive& b = prims[g[j]];
                if (pointLineDistance(b.p0, a.p0, a.p1) > tol || pointLineDistance(b.p1, a.p0, a.p1) > tol) continue;
                if (pointLineDistance(a.p0, b.p0, b.p1) > tol || pointLineDistance(a.p1, b.p0, b.p1) > tol) continue;
                const double ta0 = std::min(dot(a.p0, d), dot(a.p1, d)), ta1 = std::max(dot(a.p0, d), dot(a.p1, d));
                const double tb0 = std::min(dot(b.p0, d), dot(b.p1, d)), tb1 = std::max(dot(b.p0, d), dot(b.p1, d));
                if (tb0 > ta1 + tol || ta0 > tb1 + tol) continue;  // collinear but separated
                dsu.unite(i, j);
            }
        }
        QHash<int, std::vector<int>> comps;
        for (int i = 0; i < n; ++i) comps[dsu.find(i)].push_back(i);
        for (const auto& comp : comps) {
            if (comp.size() < 2) continue;
            int base = comp[0];
            for (int i : comp)
                if (prims[g[i]].length() > prims[g[base]].length()) base = i;
            const Primitive& bp = prims[g[base]];
            const Vec2 d = (bp.p1 - bp.p0).normalized();
            // Keep the exact coordinates of the extreme original endpoints.
            Vec2 lo = bp.p0, hi = bp.p1;
            double tlo = dot(lo, d), thi = dot(hi, d);
            int stacked = 0, drawOrder = 0;
            QStringList merged;
            for (int i : comp) {
                const Primitive& q = prims[g[i]];
                for (Vec2 e : {q.p0, q.p1}) {
                    const double t = dot(e, d);
                    if (t < tlo) { tlo = t; lo = e; }
                    if (t > thi) { thi = t; hi = e; }
                }
                stacked += q.stacked;
                drawOrder = std::max(drawOrder, q.drawOrder);
                if (i != base) { merged << q.sourceId << q.mergedFrom; rm[g[i]] = 1; }
            }
            Primitive& out = prims[g[base]];
            out.p0 = lo;
            out.p1 = hi;
            out.stacked = stacked;
            out.mergedFrom << merged;
            out.drawOrder = drawOrder;  // drawn as its topmost piece
        }
    }
    compact(prims, rm);
}

void mergeCocircularArcs(std::vector<Primitive>& prims, const CanonSettings& s) {
    const double tol = s.linTol;
    QHash<QString, std::vector<int>> groups;
    for (int i = 0; i < static_cast<int>(prims.size()); ++i)
        if (prims[i].kind == PrimKind::Arc || prims[i].kind == PrimKind::Circle) groups[groupKey(prims[i], s)].push_back(i);

    std::vector<char> rm(prims.size(), 0);
    for (auto it = groups.begin(); it != groups.end(); ++it) {
        const std::vector<int>& g = it.value();
        const int n = static_cast<int>(g.size());
        if (n < 2) continue;
        SpatialGrid grid(4 * tol);
        PieceSets dsu(n, prims, g, s);
        for (int i = 0; i < n; ++i) {
            const Primitive& a = prims[g[i]];
            grid.forNear(a.center, [&](int j) {
                const Primitive& b = prims[g[j]];
                if (dist(a.center, b.center) <= tol && std::fabs(a.radius - b.radius) <= tol) dsu.unite(i, j);
            });
            grid.insert(a.center, i);
        }
        QHash<int, std::vector<int>> comps;
        for (int i = 0; i < n; ++i) comps[dsu.find(i)].push_back(i);
        for (const auto& comp : comps) {
            if (comp.size() < 2) continue;
            // A circle absorbs every co-circular arc on it (stacked duplicate).
            int circle = -1;
            for (int i : comp)
                if (prims[g[i]].kind == PrimKind::Circle) { circle = i; break; }
            if (circle >= 0) {
                for (int i : comp) {
                    if (i == circle) continue;
                    prims[g[circle]].stacked += 1 + prims[g[i]].stacked;
                    prims[g[circle]].mergedFrom << prims[g[i]].sourceId;
                    prims[g[circle]].drawOrder = std::max(prims[g[circle]].drawOrder, prims[g[i]].drawOrder);
                    rm[g[i]] = 1;
                }
                continue;
            }
            // Arcs only: union of angular intervals.
            const double r = prims[g[comp[0]]].radius;
            const double angTol = std::max(s.angTol, tol / std::max(r, 1e-12));
            struct Iv { double s, sw; int i; };
            std::vector<Iv> iv;
            for (int i : comp) iv.push_back({prims[g[i]].a0, prims[g[i]].sweep(), i});
            // x is "continued" if some arc carries on past it: x lies in [w.s - tol, w.end - tol).
            auto continued = [&](double x) {
                for (const Iv& w : iv) {
                    const double rel = ccwSweep(w.s, x);
                    if (rel >= kTwoPi - angTol || rel < w.sw - angTol) return true;
                }
                return false;
            };
            // A cut point is the end of an arc that nothing continues from (start of a gap).
            double cut = 0;
            bool haveCut = false;
            for (const Iv& v : iv) {
                const double e = normalizeAngle(v.s + v.sw);
                if (!continued(e)) { cut = e; haveCut = true; break; }
            }
            Primitive& base = prims[g[comp[0]]];
            if (!haveCut) {  // contiguous all the way round: a full circle
                base.kind = PrimKind::Circle;
                base.a0 = base.a1 = 0;
                for (size_t k = 1; k < comp.size(); ++k) {
                    base.mergedFrom << prims[g[comp[k]]].sourceId;
                    base.stacked += prims[g[comp[k]]].stacked;
                    base.drawOrder = std::max(base.drawOrder, prims[g[comp[k]]].drawOrder);
                    rm[g[comp[k]]] = 1;
                }
                continue;
            }
            // Linear sweep starting just after the cut.
            for (Iv& v : iv) v.s = ccwSweep(cut, v.s);
            std::sort(iv.begin(), iv.end(), [](const Iv& a, const Iv& b) { return a.s < b.s; });
            size_t k = 0;
            while (k < iv.size()) {
                size_t m = k;
                double end = iv[k].s + iv[k].sw;
                int endArc = iv[k].i;
                while (m + 1 < iv.size() && iv[m + 1].s <= end + angTol) {
                    ++m;
                    if (iv[m].s + iv[m].sw > end) { end = iv[m].s + iv[m].sw; endArc = iv[m].i; }
                }
                if (m > k) {
                    Primitive& out = prims[g[iv[k].i]];
                    const double newA1 = prims[g[endArc]].a1;
                    for (size_t q = k + 1; q <= m; ++q) {
                        out.mergedFrom << prims[g[iv[q].i]].sourceId;
                        out.stacked += prims[g[iv[q].i]].stacked;
                        out.drawOrder = std::max(out.drawOrder, prims[g[iv[q].i]].drawOrder);
                        rm[g[iv[q].i]] = 1;
                    }
                    out.a1 = newA1;
                    if (end - iv[k].s >= kTwoPi - angTol) { out.kind = PrimKind::Circle; out.a0 = out.a1 = 0; }
                }
                k = m + 1;
            }
        }
    }
    compact(prims, rm);
}

// The edges a linear or aligned dimension measures from (D78): for each extension-line origin,
// the longest line of the drawing through it that runs parallel to the extension lines (across
// the measurement). A dimension from other points on those edges measures the same distance.
// A centerline counts along its whole length (its length is free, as in matching), and so does
// the line across through a circle's or arc's center, drawn or not: a distance to a hole's
// center may be given from any point on its centerline (user decision).
static void findDimensionEdges(std::vector<Primitive>& prims, const CanonSettings& s) {
    std::vector<int> lines, rounds;
    for (int i = 0; i < static_cast<int>(prims.size()); ++i) {
        if (prims[i].kind == PrimKind::Line && prims[i].length() > s.linTol) lines.push_back(i);
        if (prims[i].kind == PrimKind::Circle || prims[i].kind == PrimKind::Arc) rounds.push_back(i);
    }
    for (Primitive& p : prims) {
        if (p.kind != PrimKind::Dimension || !p.dimPlace || !isLinearDimType(p.dimType)) continue;
        const Vec2 d = p.dimPlace->dir.normalized();
        if (d.length() == 0) continue;
        auto place = std::make_shared<DimPlace>(*p.dimPlace);
        auto edgeAt = [&](Vec2 q, bool* has, bool* endless, Vec2* a, Vec2* b) {
            double longest = 0;
            bool centerline = false;
            for (int i : lines) {
                const Primitive& e = prims[i];
                const double len = e.length();
                if (pointSegmentDistance(q, e.p0, e.p1) > s.linTol) continue;
                if (std::fabs(dot((e.p1 - e.p0) / len, d)) > s.angTol + 2 * s.linTol / len) continue;  // not across
                centerline = centerline || e.ltClass == LtClass::Center;
                if (len > longest) {
                    longest = len;
                    *has = true;
                    *a = e.p0;
                    *b = e.p1;
                }
            }
            for (int i : rounds) centerline = centerline || dist(prims[i].center, q) <= s.linTol;
            if (centerline) {  // the endless line across through q
                *has = true;
                *endless = true;
                *a = q;
                *b = q + d.perp();
            }
        };
        edgeAt(p.p0, &place->hasEdge0, &place->edge0Endless, &place->edge0a, &place->edge0b);
        edgeAt(p.p1, &place->hasEdge1, &place->edge1Endless, &place->edge1a, &place->edge1b);
        p.dimPlace = std::move(place);
    }
}

void postProcess(Drawing& d, const CanonSettings& s) {
    normalizeArcs(d.prims, s);
    dropDegenerate(d.prims, s);
    removeStackedDuplicates(d.prims, s);
    mergeCollinearLines(d.prims, s);
    mergeCocircularArcs(d.prims, s);
    if (s.dimensionEdges) findDimensionEdges(d.prims, s);

    std::vector<int> stackedIdx, unsupportedIdx;
    int stackedTotal = 0;
    QMap<QString, int> unsupportedTypes;
    for (int i = 0; i < static_cast<int>(d.prims.size()); ++i) {
        const auto& p = d.prims[i];
        if (p.stacked > 0) { stackedIdx.push_back(i); stackedTotal += p.stacked; }
        if (p.kind == PrimKind::Unsupported) {
            unsupportedIdx.push_back(i);
            unsupportedTypes[p.sourceType.section('>', -1)]++;
        }
    }
    if (stackedTotal)
        d.addWarning(WarnKind::StackedDuplicate,
                     QStringLiteral("removed %1 stacked duplicate entit%2").arg(stackedTotal).arg(stackedTotal == 1 ? "y" : "ies"),
                     stackedIdx);
    if (!unsupportedIdx.empty()) {
        QStringList parts;
        for (auto it = unsupportedTypes.begin(); it != unsupportedTypes.end(); ++it)
            parts << QStringLiteral("%1 x%2").arg(it.key()).arg(it.value());
        d.addWarning(WarnKind::Unsupported,
                     QStringLiteral("%1 unsupported entit%2 not compared (%3)")
                         .arg(unsupportedIdx.size()).arg(unsupportedIdx.size() == 1 ? "y" : "ies").arg(parts.join(", ")),
                     unsupportedIdx);
    }
}

Drawing canonicalize(const DwgJson& json, const CanonSettings& s) {
    Drawing d;
    extractMeta(json, &d.meta);
    Builder b(json, s, d);
    b.run();
    b.finish();
    postProcess(d, s);
    return d;
}

std::shared_ptr<Drawing> loadDrawing(const QString& path, const IDrawingLoader& loader, const CanonSettings& s,
                                     QString* error) {
    LoadOutcome lo = loader.load(path);
    if (!lo.ok) {
        if (error) *error = lo.error;
        return nullptr;
    }
    auto d = std::make_shared<Drawing>(canonicalize(lo.json, s));
    d->meta.path = path;
    d->meta.fileName = QFileInfo(path).fileName();
    d->meta.sha256 = sha256File(path, &d->meta.size);
    d->meta.parser = loader.parserVersion();
    for (const QString& w : lo.warnings) d->addWarning(WarnKind::Load, w);
    return d;
}

}  // namespace dc
