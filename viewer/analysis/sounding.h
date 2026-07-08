#pragma once

#include <limits>
#include <utility>
#include <vector>

#include "viewer/core/field.h"
#include "viewer/core/geo.h"

namespace met::analysis {

// One level of a sounding: pressure (hPa), temperature and dewpoint (K, dewpoint
// NaN if no humidity data), and the earth-relative wind (m/s, NaN if no U/V data).
struct SoundingLevel {
    double pressure;
    float tempK;
    float dewpointK;
    float windU = std::numeric_limits<float>::quiet_NaN();  // eastward (m/s)
    float windV = std::numeric_limits<float>::quiet_NaN();  // northward (m/s)
};

struct Sounding {
    core::LatLon point;
    std::vector<SoundingLevel> levels;  // sorted top (low p) to bottom (high p)
};

// Dewpoint (K) from temperature (K) and relative humidity (%), Magnus formula.
[[nodiscard]] float dewpointFromRH(float tempK, float rhPercent);

// Extract a sounding at `point`. tStack pairs pressure with a temperature field;
// rhStack (optional, may be empty) pairs pressure with a relative-humidity field
// used to derive dewpoint. uStack/vStack (optional) pair pressure with the wind
// components; when both are present the level's earth-relative wind is filled in
// (grid-relative components on a projected grid are rotated to east/north).
[[nodiscard]] Sounding extractSounding(
    const std::vector<std::pair<double, core::Field2D>>& tStack,
    const std::vector<std::pair<double, core::Field2D>>& rhStack, core::LatLon point,
    const std::vector<std::pair<double, core::Field2D>>& uStack = {},
    const std::vector<std::pair<double, core::Field2D>>& vStack = {});

}  // namespace met::analysis
