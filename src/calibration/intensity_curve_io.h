#pragma once

#include "model/intensity_curve.h"
#include <string>

namespace qrsdp {

/// Save curve table and tail rule to JSON file. Format: {"values": [v1, v2, ...], "tail": "FLAT"|"ZERO"}.
bool saveCurveToJson(const std::string& path, const IntensityCurve& curve);

/// Load curve from JSON file. Returns true and sets curve on success.
bool loadCurveFromJson(const std::string& path, IntensityCurve& curve);

}  // namespace qrsdp
