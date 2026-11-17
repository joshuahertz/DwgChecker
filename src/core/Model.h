#pragma once
// Data model: DrawingMeta (file metadata), Primitive (one canonical piece of
// geometry in 2D WCS) and Drawing (both plus warnings).

#include "Geometry.h"
#include "Spline.h"

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

#include <memory>
#include <vector>

namespace dc {

// A DWG TIMEBLL value: Julian day + milliseconds within the day (see FIELDMAP.md).
struct DwgTime {
    qint64 day = 0;
    qint64 ms = 0;
    bool valid = false;
    bool operator==(const DwgTime& o) const { return valid && o.valid && day == o.day && ms == o.ms; }
    QDateTime toDateTime() const;  // interprets day as a Julian day number
    QString toString() const;
};

struct DrawingMeta {
    QString path;
    QString fileName;
    QByteArray sha256;  // hex
    qint64 size = 0;
    QString dwgVersion;      // AC1032
    QString dwgVersionName;  // R2018
    DwgTime tdcreate, tducreate, tdupdate, tduupdate;
    double tdindwgSeconds = -1;  // total editing time; -1 = unknown
    QString fingerprintGuid;
    QString versionGuid;
    quint64 handseed = 0;
    // SummaryInfo
    QString title, subject, author, keywords, comments, lastSavedBy, revisionNumber, hyperlinkBase;
    QList<QPair<QString, QString>> customProps;
    bool hasSummaryInfo = false;
    // AppInfo: application that last saved the file
    QString appName, appBuildVersion, appRegistryVersion, appInfoText;
    int insUnits = -1;
    int entityCount = 0;
    QString parser;  // e.g. "dwgread 0.14 (sha256 1ad7e153...)"
};

QString insUnitsName(int insunits);
QString dwgVersionName(const QString& acVersion);

// Ellipse: a whole ellipse; EllipseArc: part of one (D98). Spline: any spline (D99).
enum class PrimKind { Line, Arc, Circle, Ellipse, EllipseArc, Spline, Text, Dimension, Hatch, Unsupported };
enum class LtClass { Visible, Hidden, Center, Phantom, Other };

QString kindName(PrimKind k);
QString ltClassName(LtClass c);

// Lineweights (D100), in hundredths of a millimeter as AutoCAD lists them (25 = 0.25 mm).
constexpr int kLineweightByLayer = -1, kLineweightByBlock = -2, kLineweightDefault = -3;
// The lineweight a code in the file stands for: 0 to 23 are 0.00 to 2.11 mm; 29 ByLayer, 30
// ByBlock, 31 Default (and anything else Default).
int lineweightFromCode(int code);
// What a lineweight prints as. Default is set per computer, not in the file: it counts as
// 0.25 mm, AutoCAD's own default.
inline int plottedLineweight(int lw) { return lw < 0 ? 25 : lw; }
// "0.50 mm", "Default (0.25 mm)".
QString lineweightName(int lw);
bool isGeometric(PrimKind k);  // Line, Arc, Circle, Ellipse, EllipseArc, Spline
bool isElliptic(PrimKind k);   // Ellipse, EllipseArc
// ANG2LN / ANG3PT: the measurement is an angle in radians. Arc length is a length.
bool isAngularDimType(const QString& dimType);
// LINEAR (horizontal, vertical, rotated) and ALIGNED: a distance between two points.
bool isLinearDimType(const QString& dimType);
// Dimension types that can stand for each other: the same type, or linear and aligned (D78).
bool sameDimFamily(const QString& a, const QString& b);

// Where a dimension measures, beyond its points (D78).
struct DimPlace {
    Vec2 dir;                    // linear / aligned: the direction it measures along (any length)
    Vec2 side0, side1;           // angular: its two sides from the vertex (any length)
    bool sidesAreLines = false;  // ANG2LN: the sides are lines, either way along them
    // Linear / aligned: for each extension-line origin (p0, p1), the drawing's line through it
    // that runs parallel to the extension lines, when there is one: an edge it measures from.
    bool hasEdge0 = false, hasEdge1 = false;
    Vec2 edge0a, edge0b, edge1a, edge1b;
    // The edge is an endless line through a and b: a centerline, whose length is free, or the
    // line across through a circle's or arc's center, drawn or not (user decision).
    bool edge0Endless = false, edge1Endless = false;
};

// How a dimension is drawn: the lines, arcs and arrowheads of the block AutoCAD keeps for it
// (*D), for the views that show a file as drawn (D87). Display only, never compared; its text
// is the dimension's label instead.
struct DimShape {
    std::vector<Vec2> pts;  // a polyline (a line is two points, an arc flattened), or a filled polygon
    bool fill = false;      // a filled arrowhead (SOLID)
    int aci = 7;            // its effective color, as for a primitive
    quint32 rgb = 0xFFFFFF;
};
struct DimLook {
    std::vector<DimShape> shapes;
};

// One family of a hatch pattern's parallel lines (D101): their direction and the space between
// them, as drawn.
struct HatchFamily {
    Vec2 dir;
    double spacing = 0;
};

struct Primitive {
    PrimKind kind = PrimKind::Unsupported;
    // Geometry, 2D WCS.
    Vec2 p0, p1;            // Line and Spline endpoints; Dimension definition points (Ordinate: the feature, leader end)
    Vec2 center;            // Arc/Circle/Ellipse center; Spline: a point on it halfway along; Text/Dimension/Unsupported anchor
    // Dimension: the center or vertex it measures from (radius, diameter, angular, arc length).
    Vec2 dimCenter;
    bool hasDimCenter = false;
    std::shared_ptr<const DimPlace> dimPlace;  // linear, aligned and angular dimensions; shared so copies stay cheap
    std::shared_ptr<const DimLook> dimLook;    // how it is drawn, when the file has its block (D87); not kept by transformed()
    double radius = 0;      // Arc/Circle radius; Ellipse: the major semi-axis
    // Ellipse (D98): the minor semi-axis (at most `radius`), and the major axis's direction in
    // [0, pi). A point of it is center + u*radius*cos(t) + perp(u)*minorRadius*sin(t), with u
    // along the major axis: t is the ellipse's parameter, as in AutoCAD.
    double minorRadius = 0;
    double axisAngle = 0;
    double a0 = 0, a1 = 0;  // Arc start/end angle, EllipseArc start/end parameter: CCW, normalized to [0, 2pi)
    std::shared_ptr<const SplineData> spline;  // Spline: the curve (D99); shared so copies stay cheap
    // As drawn, for the readability checks (D101); transformed() leaves them as they are.
    double ltPattern = 0;   // a dashed linetype's pattern length: LTSCALE x the object's own scale x a block's; 0 continuous
    double ltGap = 0;       // ...and its shortest gap, likewise
    double ltRun = 0;       // the length the pattern runs along, when longer than its own (polyline linetype generation)
    double dimTextHeight = 0, dimArrowSize = 0;  // Dimension: its text height and arrowhead size
    std::shared_ptr<const std::vector<HatchFamily>> hatchLines;  // Hatch: its pattern's families of lines
    // Annotation payload.
    QString text;           // Text content; Dimension user text; Hatch: normalized pattern name
    double measurement = 0; // Dimension measured value
    QString dimType;        // LINEAR, ALIGNED, DIAMETER, RADIUS, ANG2LN, ...
    // Dimension display format: its style plus per-dimension overrides (DimText.h).
    int dimUnit = -1;       // DIMLUNIT (linear) or DIMAUNIT (angular); -1 = no style information
    int dimPrec = 0;        // DIMDEC or DIMADEC
    int dimZin = 0;         // DIMZIN or DIMAZIN (zero suppression)
    double dimLfac = 1;     // DIMLFAC (linear)
    double dimRnd = 0;      // DIMRND (linear)
    QChar dimSep = QLatin1Char('.');  // DIMDSEP
    // Provenance.
    QString layer;
    QString ltypeName;      // effective linetype after ByLayer/ByBlock
    LtClass ltClass = LtClass::Visible;
    // Effective color after ByLayer/ByBlock: ACI number (-1 for a true color) and RGB.
    int aci = 7;
    quint32 rgb = 0xFFFFFF;
    bool colorByLayer = true;  // the color comes from the layer, not set on the object
    bool ltByLayer = true;     // likewise the linetype
    // Effective lineweight after ByLayer/ByBlock (D100): hundredths of a mm, or kLineweightDefault.
    int lineweight = kLineweightDefault;
    bool lwByLayer = true;     // likewise the lineweight
    // Its top-level entity's place in AutoCAD's draw order (DRAWORDER / SORTENTSTABLE), for
    // showing the file as AutoCAD does. Display only: the primitives stay in file order.
    int drawOrder = 0;
    QString sourceType;     // e.g. LINE, LWPOLYLINE, INSERT>ARC
    QString sourceId;       // stable id: handle path "29C/27A#3" (hex handles, segment index)
    quint64 handle = 0;     // handle of the top-level entity it came from
    QStringList mergedFrom; // other source ids merged into this one
    int stacked = 0;        // number of exact duplicates removed on top of this one
    int view = -1;          // Orthographic mode: reference view id
    // Hatch: closed boundary loops, arcs flattened, WCS. Shared so copies stay cheap.
    std::shared_ptr<const std::vector<std::vector<Vec2>>> loops;
    bool evenOdd = true;    // Hatch fill rule: parity over all loops (false: union of the loops)

    double length() const { return kind == PrimKind::Line ? dist(p0, p1) : 0; }
    double sweep() const;   // Arc, EllipseArc: CCW sweep in (0, 2pi); Circle, Ellipse: 2pi
    Vec2 midpoint() const;  // a representative point (line midpoint, arc midpoint, center)
    // The point at angle t of an arc or circle, or at parameter t of an ellipse.
    Vec2 pointAt(double t) const {
        if (!isElliptic(kind)) return center + fromAngle(t) * radius;
        const Vec2 u = fromAngle(axisAngle);
        return center + u * (radius * std::cos(t)) + u.perp() * (minorRadius * std::sin(t));
    }
    Vec2 arcStart() const { return pointAt(a0); }
    Vec2 arcEnd() const { return pointAt(a1); }
    BBox bbox() const;
    // A rough size used for tie-breaking: length, or length along the curve.
    double size() const;
};

Primitive transformed(const Primitive& p, const Transform2D& t);

// Makes p the ellipse, or the part of it from parameter t0 to t1 (CCW), traced by
// c + U cos(t) + V sin(t) (D98). U and V need not be its axes (a circle in a block scaled
// unevenly gives two that are not); they must not be parallel. The axes are found from them,
// the major one turned into [0, pi), and a clockwise trace (a mirror) made counter-clockwise.
void setEllipse(Primitive& p, Vec2 c, Vec2 U, Vec2 V, double t0, double t1, bool full);
// The same ellipse, or elliptical arc, within the tolerances: center, both axes and the major
// axis's direction (an angle within angTol, or turned by less than linTol at its ends, as the
// difference of the axes moves them); an arc's two ends too.
bool sameEllipse(const Primitive& a, const Primitive& b, double linTol, double angTol);

// The point a primitive is found by in spatial lookups: a line's midpoint, otherwise its center.
inline Vec2 keyPoint(const Primitive& p) { return p.kind == PrimKind::Line ? p.midpoint() : p.center; }

// Hatch region helpers.
bool hatchContains(const Primitive& hatch, Vec2 q);
double hatchBoundaryDistance(const Primitive& hatch, Vec2 q);

enum class WarnKind { Unsupported, NonZeroZ, StackedDuplicate, Units, HiddenLayer, Xref, Load, Other };

struct DrawingWarning {
    WarnKind kind = WarnKind::Other;
    QString text;
    std::vector<int> prims;  // indices into Drawing::prims, when relevant
};

struct LayerInfo {
    QString name;
    QString ltype;
    int color = 7;           // ACI number, or -1 for a true color
    quint32 rgb = 0xFFFFFF;  // the color as RGB
    int lineweight = kLineweightDefault;  // hundredths of a mm (D100)
    bool frozen = false, off = false, locked = false, plot = true;
};

// A viewport of one of the drawing's layouts (D101): the part of model space it shows, at
// what scale, as printed.
struct LayoutViewport {
    BBox window;              // in model space
    double scale = 1;         // paper units per model unit
    double area = 0;          // its size on the sheet, in paper units squared
    double mmPerUnit = 25.4;  // the sheet's unit in millimeters (inches unless the layout plots in mm)
};

struct Drawing {
    DrawingMeta meta;
    std::vector<Primitive> prims;
    std::vector<DrawingWarning> warnings;
    // Objects on turned-off or frozen layers: left out of prims when the reading settings
    // skip them, else included (the HiddenLayer warning says which).
    int hiddenLayerObjects = 0;
    bool hiddenLayerObjectsSkipped = false;
    std::vector<LayerInfo> layers;
    std::vector<LayoutViewport> viewports;  // every layout's, but each layout's sheet viewport (D101)
    bool psltscale = true;  // PSLTSCALE: in a viewport, linetypes keep their size on the sheet
    // Top-level entity handles and types in handle order (informational duplicate signal).
    std::vector<quint64> entityHandles;
    QStringList entityTypes;

    BBox bbox(bool geometricOnly = true) const;
    int count(PrimKind k) const;
    void addWarning(WarnKind k, const QString& text, std::vector<int> prims = {});
};

using DrawingPtr = std::shared_ptr<const Drawing>;

}  // namespace dc
