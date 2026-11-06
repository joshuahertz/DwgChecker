#include "DimText.h"

#include <QRegularExpression>

#include <algorithm>
#include <cmath>

namespace dc {

namespace {

const QChar kDiameterSign(0x00D8);
const QChar kDegreeSign(0x00B0);

bool isTypedText(const QString& t) { return !t.isEmpty() && !t.contains(QLatin1String("<>")); }

// Decimal zero suppression: trailing zeros (and a bare separator), and the "0" of "0.625".
QString suppressZeros(QString s, bool leading, bool trailing) {
    if (trailing && s.contains(QLatin1Char('.'))) {
        while (s.endsWith(QLatin1Char('0'))) s.chop(1);
        if (s.endsWith(QLatin1Char('.'))) s.chop(1);
    }
    if (leading && s.startsWith(QLatin1String("0.")) && s.size() > 2) s.remove(0, 1);
    return s;
}

QString decimalText(double v, int prec, bool leading, bool trailing, QChar sep) {
    // Halves round up, as AutoCAD does (0.625 at two places reads 0.63).
    const double scale = std::pow(10.0, prec);
    QString s = suppressZeros(QString::number(std::round(v * scale) / scale, 'f', prec), leading, trailing);
    if (sep != QLatin1Char('.')) s.replace(QLatin1Char('.'), sep);
    return s;
}

// A value rounded to 1/2^prec and reduced: 1.5 -> {1, 1, 2}.
struct Frac {
    long long whole = 0, num = 0, den = 1;
};
Frac toFrac(double v, int prec) {
    const long long d = 1LL << prec;
    const long long n = std::llround(v * static_cast<double>(d));
    Frac f{n / d, n % d, d};
    if (f.num == 0) f.den = 1;
    while (f.num > 0 && f.num % 2 == 0) {
        f.num /= 2;
        f.den /= 2;
    }
    return f;
}
QString fracText(const Frac& f) {
    if (f.num == 0) return QString::number(f.whole);
    const QString fr = QStringLiteral("%1/%2").arg(f.num).arg(f.den);
    return f.whole == 0 ? fr : QStringLiteral("%1 %2").arg(f.whole).arg(fr);
}

// Feet and inches. DIMZIN bits 0-1: 0 drops zero feet and zero inches, 1 keeps
// both, 2 keeps zero feet only, 3 keeps zero inches only.
QString feetInches(long long feet, const QString& inches, bool inchesZero, int zin) {
    const int z = zin & 3;
    const bool keepZeroFeet = z == 1 || z == 2;
    const bool keepZeroInches = z == 1 || z == 3;
    const QString in = inches + QLatin1Char('"');
    if (feet == 0) return keepZeroFeet ? QStringLiteral("0'-") + in : in;
    if (inchesZero && !keepZeroInches) return QStringLiteral("%1'").arg(feet);
    return QStringLiteral("%1'-%2").arg(feet).arg(in);
}

QString linearText(const Primitive& p) {
    double v = std::fabs(p.measurement) * std::fabs(p.dimLfac);
    if (p.dimRnd > 0) v = std::round(v / p.dimRnd) * p.dimRnd;
    const int prec = std::clamp(p.dimPrec, 0, 8);
    const bool leading = p.dimZin & 4, trailing = p.dimZin & 8;
    switch (p.dimUnit) {
    case 1: {  // scientific
        QString s = QString::number(v, 'E', prec);
        if (p.dimSep != QLatin1Char('.')) s.replace(QLatin1Char('.'), p.dimSep);
        return s;
    }
    case 3: {  // engineering: feet and decimal inches
        const double scale = std::pow(10.0, prec);
        const double r = std::round(v * scale) / scale;
        const long long feet = static_cast<long long>(std::floor(r / 12.0 + 1e-12));
        const double inches = std::max(0.0, r - 12.0 * static_cast<double>(feet));
        return feetInches(feet, decimalText(inches, prec, leading, trailing, p.dimSep), inches < 0.5 / scale, p.dimZin);
    }
    case 4: {  // architectural: feet and fractional inches
        const Frac f = toFrac(v, prec);
        const Frac in{f.whole % 12, f.num, f.den};
        return feetInches(f.whole / 12, fracText(in), in.whole == 0 && in.num == 0, p.dimZin);
    }
    case 5: return fracText(toFrac(v, prec));  // fractional
    default: return decimalText(v, prec, leading, trailing, p.dimSep);  // 2 decimal, 6 Windows desktop
    }
}

QString angularText(const Primitive& p) {
    const double rad = std::fabs(p.measurement);
    const double deg = rad * 180.0 / kPi;
    const int prec = std::clamp(p.dimPrec, 0, 8);
    const bool leading = p.dimZin & 1, trailing = p.dimZin & 2;
    switch (p.dimUnit) {
    case 1:
    case 4: {  // degrees/minutes/seconds (surveyor's units use the same precision steps)
        if (prec == 0) return QString::number(std::llround(deg)) + kDegreeSign;
        if (prec <= 2) {
            const long long m = std::llround(deg * 60.0);
            return QStringLiteral("%1%2%3'").arg(m / 60).arg(kDegreeSign).arg(m % 60);
        }
        if (prec <= 4) {
            const long long s = std::llround(deg * 3600.0);
            return QStringLiteral("%1%2%3'%4\"").arg(s / 3600).arg(kDegreeSign).arg((s / 60) % 60).arg(s % 60);
        }
        const int sd = prec - 4;
        const double scale = std::pow(10.0, sd);
        const double total = std::round(deg * 3600.0 * scale) / scale;
        const long long ws = static_cast<long long>(std::floor(total + 1e-9));
        const double sec = static_cast<double>(ws % 60) + (total - static_cast<double>(ws));
        return QStringLiteral("%1%2%3'%4\"").arg(ws / 3600).arg(kDegreeSign).arg((ws / 60) % 60).arg(QString::number(sec, 'f', sd));
    }
    case 2: return decimalText(rad * 200.0 / kPi, prec, leading, trailing, p.dimSep) + QLatin1Char('g');
    case 3: return decimalText(rad, prec, leading, trailing, p.dimSep) + QLatin1Char('r');
    default: return decimalText(deg, prec, leading, trailing, p.dimSep) + kDegreeSign;
    }
}

// See dimPrecisionKey.
QString keyOf(const QString& s) {
    QString out;
    for (int i = 0; i < s.size();) {
        const QChar c = s[i];
        if (c.isDigit()) {
            int j = i;
            while (j < s.size() && s[j].isDigit()) ++j;
            const QChar prev = i > 0 ? s[i - 1] : QChar();
            if (prev == QLatin1Char('.') || prev == QLatin1Char(','))
                out += QLatin1Char('.') + QString(j - i, QLatin1Char('#'));
            else if (prev == QLatin1Char('/'))
                out += QLatin1Char('/') + s.mid(i, j - i);
            i = j;
            continue;
        }
        if (c == kDegreeSign || c == QLatin1Char('\'') || c == QLatin1Char('"')) out += c;
        ++i;
    }
    return out;
}

double numberValue(const QString& t) {
    const QString s = t.trimmed();
    const int slash = s.indexOf(QLatin1Char('/'));
    if (slash < 0) return QString(s).replace(QLatin1Char(','), QLatin1Char('.')).toDouble();
    const int space = s.lastIndexOf(QLatin1Char(' '), slash);
    const double whole = space > 0 ? s.left(space).toDouble() : 0.0;
    const QString fr = space > 0 ? s.mid(space + 1) : s;
    const int sl = fr.indexOf(QLatin1Char('/'));
    const double den = fr.mid(sl + 1).toDouble();
    return whole + (den > 0 ? fr.left(sl).toDouble() / den : 0.0);
}

// Typed-in text: the precision of the number closest to the measured value, so
// that "2X Ø.750 THRU" reads as ".###".
std::optional<QString> typedKey(const Primitive& p) {
    static const QRegularExpression num(QStringLiteral("(\\d+\\s+\\d+/\\d+|\\d+/\\d+|\\d*[.,]\\d+|\\d+)\\s*([\"'\\x{00B0}])?"));
    const QString t = plainMtext(p.text);
    const double target = isAngularDimType(p.dimType) ? std::fabs(p.measurement) * 180.0 / kPi
                                                      : std::fabs(p.measurement) * std::fabs(p.dimLfac);
    std::optional<QString> best;
    double bestDiff = 0;
    for (auto it = num.globalMatch(t); it.hasNext();) {
        const QRegularExpressionMatch m = it.next();
        const double d = std::fabs(numberValue(m.captured(1)) - target);
        if (!best || d < bestDiff) {
            bestDiff = d;
            best = keyOf(m.captured(0));
        }
    }
    return best;
}

QString unitName(const Primitive& p) {
    if (isAngularDimType(p.dimType)) {
        switch (p.dimUnit) {
        case 1: return QStringLiteral("degrees/minutes/seconds");
        case 2: return QStringLiteral("gradian");
        case 3: return QStringLiteral("radian");
        case 4: return QStringLiteral("surveyor's");
        default: return QStringLiteral("decimal degree");
        }
    }
    switch (p.dimUnit) {
    case 1: return QStringLiteral("scientific");
    case 3: return QStringLiteral("engineering");
    case 4: return QStringLiteral("architectural");
    case 5: return QStringLiteral("fractional");
    default: return QStringLiteral("decimal");
    }
}

int unitFamily(const Primitive& p) { return !isAngularDimType(p.dimType) && p.dimUnit == 6 ? 2 : p.dimUnit; }

}  // namespace

bool dimTextIsTyped(const Primitive& p) { return p.kind == PrimKind::Dimension && isTypedText(p.text); }

QString dimValueText(const Primitive& p) {
    if (p.kind != PrimKind::Dimension) return {};
    if (isTypedText(p.text)) return plainMtext(p.text).trimmed();
    if (p.dimUnit < 0) return {};
    return isAngularDimType(p.dimType) ? angularText(p) : linearText(p);
}

QString dimShownText(const Primitive& p) {
    const QString v = dimValueText(p);
    if (v.isEmpty() || isTypedText(p.text)) return v;  // typed-in text replaces the prefix too
    if (p.dimType == QLatin1String("DIAMETER")) return kDiameterSign + v;
    if (p.dimType == QLatin1String("RADIUS") || p.dimType == QLatin1String("LARGE_RADIAL_DIMENSION")) return QLatin1Char('R') + v;
    return v;
}

std::optional<QString> dimPrecisionKey(const Primitive& p) {
    if (p.kind != PrimKind::Dimension) return std::nullopt;
    if (isTypedText(p.text)) return typedKey(p);
    if (p.dimUnit < 0) return std::nullopt;
    return keyOf(dimValueText(p));
}

bool dimPrecisionDiffers(const Primitive& ref, const Primitive& stu) {
    const auto a = dimPrecisionKey(ref), b = dimPrecisionKey(stu);
    return a && b && *a != *b;
}

QString dimPrecisionReason(const Primitive& ref, const Primitive& stu) {
    if (isTypedText(stu.text)) return QStringLiteral("typed-in text");
    if (ref.dimUnit < 0 || stu.dimUnit < 0 || isTypedText(ref.text)) return {};
    if (unitFamily(ref) != unitFamily(stu))
        return QStringLiteral("%1 units instead of %2").arg(unitName(stu), unitName(ref));
    static const QRegularExpression decimals(QStringLiteral("^(\\.#+)?\\x{00B0}?$"));
    const QString rk = keyOf(dimValueText(ref)), sk = keyOf(dimValueText(stu));
    if (!decimals.match(rk).hasMatch() || !decimals.match(sk).hasMatch()) return {};
    const int trailingBit = isAngularDimType(stu.dimType) ? 2 : 8;
    if (stu.dimPrec == ref.dimPrec && (stu.dimZin & trailingBit) && !(ref.dimZin & trailingBit))
        return QStringLiteral("trailing zeros suppressed");
    const auto rd = rk.count(QLatin1Char('#')), sd = sk.count(QLatin1Char('#'));
    return QStringLiteral("%1 decimal place%2 instead of %3").arg(sd).arg(sd == 1 ? "" : "s").arg(rd);
}

QString dimKindName(const QString& t) {
    if (t == QLatin1String("LINEAR")) return QStringLiteral("linear");
    if (t == QLatin1String("ALIGNED")) return QStringLiteral("aligned");
    if (isAngularDimType(t)) return QStringLiteral("angular");
    if (t == QLatin1String("DIAMETER")) return QStringLiteral("diameter");
    if (t == QLatin1String("RADIUS") || t == QLatin1String("LARGE_RADIAL_DIMENSION")) return QStringLiteral("radius");
    if (t == QLatin1String("ORDINATE")) return QStringLiteral("ordinate");
    if (t == QLatin1String("ARC_DIMENSION")) return QStringLiteral("arc length");
    return t.toLower();
}

QString plainMtext(const QString& in) {
    static const QRegularExpression unicode(QStringLiteral("\\\\U\\+([0-9A-Fa-f]{4})"));
    static const QRegularExpression codes(QStringLiteral("\\\\[AaCcFfHhQqTtWwp][^;]*;"));
    static const QRegularExpression toggles(QStringLiteral("\\\\[LlOoKk]"));
    static const QRegularExpression stack(QStringLiteral("\\\\S([^;]*);"));

    QString s;
    int last = 0;
    for (auto it = unicode.globalMatch(in); it.hasNext();) {
        const QRegularExpressionMatch m = it.next();
        s += in.mid(last, m.capturedStart() - last);
        s += QChar(static_cast<char16_t>(m.captured(1).toUShort(nullptr, 16)));
        last = m.capturedEnd();
    }
    s += in.mid(last);
    s.replace(QLatin1String("%%c"), QString(kDiameterSign), Qt::CaseInsensitive);
    s.replace(QLatin1String("%%d"), QString(kDegreeSign), Qt::CaseInsensitive);
    s.replace(QLatin1String("%%p"), QString(QChar(0x00B1)), Qt::CaseInsensitive);
    s.replace(QChar(0x2205), kDiameterSign);  // AutoCAD's dimension text uses the empty-set sign
    s.replace(QLatin1String("\\P"), QLatin1String(" "));
    s.replace(QLatin1String("\\~"), QLatin1String(" "));
    s.remove(codes);
    s.remove(toggles);
    s.remove(QLatin1Char('{'));
    s.remove(QLatin1Char('}'));

    QString out;
    last = 0;
    for (auto it = stack.globalMatch(s); it.hasNext();) {
        const QRegularExpressionMatch m = it.next();
        out += s.mid(last, m.capturedStart() - last);
        QString part = m.captured(1);
        part.replace(QLatin1Char('#'), QLatin1Char('/'));
        part.replace(QLatin1Char('^'), QLatin1Char(' '));
        if (!out.isEmpty() && out.back().isDigit()) out += QLatin1Char(' ');
        out += part.trimmed();
        last = m.capturedEnd();
    }
    out += s.mid(last);
    return out;
}

}  // namespace dc
