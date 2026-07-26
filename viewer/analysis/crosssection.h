#pragma once

#include <utility>
#include <vector>

#include "viewer/core/field.h"
#include "viewer/core/geo.h"

namespace met::analysis {

// A vertical cross-section: a variable sampled along a great-circle path at each
// level. values[levelIndex][sampleIndex]; NaN where off-grid/missing. `pressures`
// is per-column (pressures[levelIndex][sampleIndex], hPa) so terrain-following
// native model levels position each column at its own pressure; for isobaric data
// every column in a level shares the same value. `heights` is the geopotential
// height (gpm) on the same [level][sample] layout, empty when the dataset carries
// no height field. It is genuinely two-dimensional even for isobaric data — a
// pressure surface tilts along the path — which is why it is a field to contour
// rather than a second tick set on the vertical axis.
struct CrossSection {
    std::vector<core::LatLon> points;            // path sample points
    std::vector<double> distancesKm;             // cumulative distance along the path
    std::vector<std::vector<double>> pressures;  // [level][sample], hPa
    std::vector<std::vector<double>> heights;    // [level][sample], gpm; may be empty
    std::vector<std::vector<float>> values;      // [level][sample]
    std::string units;
};

// Build a cross-section by sampling each (pressure, field) in `levelStack` at
// `nSamples` points evenly spaced (great-circle) along the path `vertices`. Each
// level's pressure is broadcast across all columns (isobaric data). `zStack`
// (optional) pairs pressure with a geopotential-height or geopotential field and
// fills `heights`, converted to gpm through each field's own units.
[[nodiscard]] CrossSection extractCrossSection(
    const std::vector<std::pair<double, core::Field2D>>& levelStack,
    const std::vector<core::LatLon>& vertices, int nSamples,
    const std::vector<std::pair<double, core::Field2D>>& zStack = {});

// Build a cross-section from native model-level (hybrid/sigma) data. `levelStack`
// and `presStack` are keyed by model-level index; the pressure at each column is
// read from the `pres` field (`presStack`), giving a terrain-following pressure
// axis. `zStack` (optional, same keying) fills `heights`. Levels are ordered top
// (low mean pressure) to bottom.
[[nodiscard]] CrossSection extractCrossSectionModelLevels(
    const std::vector<std::pair<double, core::Field2D>>& levelStack,
    const std::vector<std::pair<double, core::Field2D>>& presStack,
    const std::vector<core::LatLon>& vertices, int nSamples,
    const std::vector<std::pair<double, core::Field2D>>& zStack = {});

}  // namespace met::analysis
