#pragma once
// How a dimension reads on the drawing, for the dimension precision check.
// The measured value is formatted the way AutoCAD formats it from the
// dimension's style and overrides (DIMLUNIT / DIMAUNIT, DIMDEC / DIMADEC,
// DIMZIN / DIMAZIN, DIMRND, DIMLFAC, DIMDSEP). tst_dims checks the result
// against the text AutoCAD itself put in every corpus dimension.

#include "Model.h"

#include <optional>

namespace dc {

// True when typed-in text replaces the measurement (user text without "<>").
bool dimTextIsTyped(const Primitive& p);
// The measurement as the drawing shows it ("4.000", "1 1/2\"", "90°"), or the
// typed-in text that replaces it. Empty when the format is unknown.
QString dimValueText(const Primitive& p);
// The same with the automatic diameter / radius prefix, for messages ("Ø0.750").
QString dimShownText(const Primitive& p);
// The precision the text shows. Digits after the decimal separator become '#',
// fraction denominators are kept, unit marks (° ' ") are kept, and everything
// else is dropped: "4.000" and ".625" give ".###", "4" gives "", "1 1/2\"" gives
// "/2\"" and "90°" gives "°". Unknown (no style information, no number in
// typed-in text): nullopt.
std::optional<QString> dimPrecisionKey(const Primitive& p);
// Both precisions are known and the drawing would show them differently.
bool dimPrecisionDiffers(const Primitive& ref, const Primitive& stu);
// Why they differ, for messages: "2 decimal places instead of 3", "trailing
// zeros suppressed", "fractional units instead of decimal", "typed-in text".
QString dimPrecisionReason(const Primitive& ref, const Primitive& stu);
// "linear", "aligned", "angular", "diameter", "radius", "ordinate", "arc length".
QString dimKindName(const QString& dimType);

// MTEXT formatting codes removed: "\A1;1{\H1x;\S1/2;}" -> "1 1/2", "%%c" -> "Ø".
QString plainMtext(const QString& s);

}  // namespace dc
