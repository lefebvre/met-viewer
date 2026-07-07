#pragma once

#include <utility>
#include <vector>

#include "viewer/core/field.h"
#include "viewer/core/geo.h"

namespace met::analysis {

// A vertical cross-section: a variable sampled along a great-circle path at each
// pressure level. values[levelIndex][sampleIndex]; NaN where off-grid/missing.
struct CrossSection {
    std::vector<core::LatLon> points;    // path sample points
    std::vector<double> distancesKm;     // cumulative distance along the path
    std::vector<double> pressures;       // one per level, as supplied
    std::vector<std::vector<float>> values;
    std::string units;
};

// Build a cross-section by sampling each (pressure, field) in `levelStack` at
// `nSamples` points evenly spaced (great-circle) along the path `vertices`.
[[nodiscard]] CrossSection extractCrossSection(
    const std::vector<std::pair<double, core::Field2D>>& levelStack,
    const std::vector<core::LatLon>& vertices, int nSamples);

}  // namespace met::analysis
