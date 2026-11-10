#pragma once
// Object and layer colors as dwgread writes them, the AutoCAD Color Index (ACI)
// palette, and how different two colors are.

#include <QJsonValue>
#include <QString>

namespace dc {

struct DwgColor {
    enum class Kind { ByLayer, ByBlock, Aci, Rgb };
    Kind kind = Kind::ByLayer;
    int aci = 7;               // Kind::Aci
    quint32 rgb = 0xFFFFFF;    // Kind::Rgb (0xRRGGBB)
};

// The "color" of an entity or a layer in dwgread's JSON. R2004+ files give
// {"index": n, "rgb": "c2RRGGBB" / "c3000007" / "000000", "flag": ...}: a method
// byte c0 ByLayer, c1 ByBlock, c2 true color, c3 ACI; without it the index says
// (256 ByLayer, 0 or absent ByBlock, 1-255 ACI). Older files give the index alone.
DwgColor parseDwgColor(const QJsonValue& v);

// RGB of an ACI number (1-255); 7 (white/black) is white.
quint32 aciToRgb(int aci);

// "red (ACI 1)", "ACI 34", "RGB 250,5,5" (aci < 0: a true color).
QString colorName(int aci, quint32 rgb);

// How far apart two colors are, 0-100: the RGB distance as a share of the largest
// one (black to white). 0 = the same color; red to blue is 82.
double colorDifference(quint32 a, quint32 b);

}  // namespace dc
