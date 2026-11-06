#pragma once

#include "DwgJson.h"
#include "Model.h"

namespace dc {

// Fills DrawingMeta from the parsed JSON (see FIELDMAP.md for every field).
// File-system fields (path, SHA-256, size) are filled by the caller.
void extractMeta(const DwgJson& json, DrawingMeta* meta);

DwgTime parseDwgTime(const QJsonValue& v);

}  // namespace dc
