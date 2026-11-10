#include "Color.h"

#include <QJsonObject>

#include <algorithm>
#include <cmath>

namespace dc {

namespace {

DwgColor fromIndex(int index) {
    DwgColor c;
    index = std::abs(index);  // a layer that is off stores its color negated
    if (index == 256) c.kind = DwgColor::Kind::ByLayer;
    else if (index == 0) c.kind = DwgColor::Kind::ByBlock;
    else if (index >= 1 && index <= 255) {
        c.kind = DwgColor::Kind::Aci;
        c.aci = index;
    }
    return c;
}

quint32 rgbOf(double r, double g, double b) {
    auto ch = [](double v) { return static_cast<quint32>(std::clamp(std::floor(v + 1e-9), 0.0, 255.0)); };
    return (ch(r) << 16) | (ch(g) << 8) | ch(b);
}

}  // namespace

DwgColor parseDwgColor(const QJsonValue& v) {
    if (v.isDouble()) return fromIndex(v.toInt());
    if (!v.isObject()) return {};
    const QJsonObject o = v.toObject();
    const QString hex = o.value(QLatin1String("rgb")).toString();
    if (hex.size() == 8) {
        bool ok = false;
        const quint32 raw = hex.toUInt(&ok, 16);
        if (ok) {
            DwgColor c;
            switch (raw >> 24) {
            case 0xC0: c.kind = DwgColor::Kind::ByLayer; return c;
            case 0xC1: c.kind = DwgColor::Kind::ByBlock; return c;
            case 0xC2: c.kind = DwgColor::Kind::Rgb; c.rgb = raw & 0xFFFFFF; return c;
            case 0xC3: return fromIndex(static_cast<int>(raw & 0xFF));
            default: break;
            }
        }
    }
    return fromIndex(o.value(QLatin1String("index")).toInt(0));
}

quint32 aciToRgb(int aci) {
    static const quint32 basic[10] = {0x000000, 0xFF0000, 0xFFFF00, 0x00FF00, 0x00FFFF,
                                      0x0000FF, 0xFF00FF, 0xFFFFFF, 0x808080, 0xC0C0C0};
    if (aci >= 0 && aci <= 9) return basic[aci];
    if (aci >= 250 && aci <= 255) {
        static const int gray[6] = {51, 91, 132, 173, 214, 255};
        const quint32 g = static_cast<quint32>(gray[aci - 250]);
        return (g << 16) | (g << 8) | g;
    }
    if (aci < 10 || aci > 249) return 0xFFFFFF;
    // 10-249: 24 hues 15 degrees apart; in each run of ten, five shades (value 255, 204,
    // 153, 127, 76), each fully saturated (even) and then half saturated (odd).
    static const double value[5] = {255, 204, 153, 127, 76};
    const int hue = (aci - 10) / 10, k = (aci - 10) % 10;
    const double v = value[k / 2], s = k % 2 ? 0.5 : 1.0;
    const double h = hue * 15.0 / 60.0;  // sextant position 0-6
    const double lo = v * (1 - s);
    const double f = h - std::floor(h);
    const double up = lo + (v - lo) * f, down = v - (v - lo) * f;
    switch (static_cast<int>(std::floor(h)) % 6) {
    case 0: return rgbOf(v, up, lo);
    case 1: return rgbOf(down, v, lo);
    case 2: return rgbOf(lo, v, up);
    case 3: return rgbOf(lo, down, v);
    case 4: return rgbOf(up, lo, v);
    default: return rgbOf(v, lo, down);
    }
}

QString colorName(int aci, quint32 rgb) {
    static const char* const names[8] = {"", "red", "yellow", "green", "cyan", "blue", "magenta", "white"};
    if (aci >= 1 && aci <= 7) return QStringLiteral("%1 (ACI %2)").arg(QLatin1String(names[aci])).arg(aci);
    if (aci >= 1 && aci <= 255) return QStringLiteral("ACI %1").arg(aci);
    return QStringLiteral("RGB %1,%2,%3").arg((rgb >> 16) & 0xFF).arg((rgb >> 8) & 0xFF).arg(rgb & 0xFF);
}

double colorDifference(quint32 a, quint32 b) {
    const double dr = double((a >> 16) & 0xFF) - double((b >> 16) & 0xFF);
    const double dg = double((a >> 8) & 0xFF) - double((b >> 8) & 0xFF);
    const double db = double(a & 0xFF) - double(b & 0xFF);
    return 100.0 * std::sqrt(dr * dr + dg * dg + db * db) / std::sqrt(3.0 * 255.0 * 255.0);
}

}  // namespace dc
