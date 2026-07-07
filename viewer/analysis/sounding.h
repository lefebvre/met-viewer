#pragma once

#include <utility>
#include <vector>

#include "viewer/core/field.h"
#include "viewer/core/geo.h"

namespace met::analysis {

// One level of a sounding: pressure (hPa), temperature and dewpoint (K, dewpoint
// NaN if no humidity data).
struct SoundingLevel {
    double pressure;
    float tempK;
    float dewpointK;
};

struct Sounding {
    core::LatLon point;
    std::vector<SoundingLevel> levels;  // sorted top (low p) to bottom (high p)
};

// Dewpoint (K) from temperature (K) and relative humidity (%), Magnus formula.
[[nodiscard]] float dewpointFromRH(float tempK, float rhPercent);

// Extract a sounding at `point`. tStack pairs pressure with a temperature field;
// rhStack (optional, may be empty) pairs pressure with a relative-humidity field
// used to derive dewpoint.
[[nodiscard]] Sounding extractSounding(
    const std::vector<std::pair<double, core::Field2D>>& tStack,
    const std::vector<std::pair<double, core::Field2D>>& rhStack, core::LatLon point);

}  // namespace met::analysis
