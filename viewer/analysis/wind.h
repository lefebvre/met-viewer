#pragma once

#include <optional>
#include <string>
#include <vector>

#include "viewer/core/field.h"
#include "viewer/core/geo.h"

namespace met::analysis {

// A canonical U/V variable-name pair.
struct WindPair {
    std::string uName;
    std::string vName;
};

// Find the first U/V wind pair among the given canonical variable names
// (matches u/v, 10u/10v, 100u/100v, ugrd/vgrd, uwnd/vwnd, case-insensitively).
[[nodiscard]] std::optional<WindPair> findWindPair(const std::vector<std::string>& varNames);

// A paired wind field. After construction the components are earth-relative
// (east/north); call rotateToEarthRelative() when the source is grid-relative on
// a projected grid.
struct WindField {
    core::Field2D u;  // eastward component
    core::Field2D v;  // northward component

    [[nodiscard]] int width() const { return u.width(); }
    [[nodiscard]] int height() const { return u.height(); }
};

// A sampled wind vector (m/s), earth-relative.
struct UV {
    float u = 0.0f;
    float v = 0.0f;
};

[[nodiscard]] UV sampleWind(const WindField& w, double x, double y);
[[nodiscard]] UV sampleWindLatLon(const WindField& w, core::LatLon at);

// Earth-relative wind sampled at a single geographic point, rotating only the one
// sampled vector rather than the whole grid. `uGrid`/`vGrid` are the (possibly
// grid-relative) components sharing a grid; when they are grid-relative on a
// projected grid the sampled vector is rotated by the local meridian convergence.
// Use this instead of copying + rotateToEarthRelative() when only a single point
// is needed (e.g. a sounding level) — it is O(1) rather than O(grid). Returns NaN
// off-domain.
[[nodiscard]] UV earthRelativeWindAt(const core::Field2D& uGrid, const core::Field2D& vGrid,
                                     core::LatLon at);

// Wind speed (magnitude) at a fractional index.
[[nodiscard]] float windSpeed(const WindField& w, double x, double y);

// Rotate grid-relative components to earth-relative in place. For a regular
// lat/lon grid this is a no-op; for a conformal projected grid it applies the
// per-point meridian-convergence rotation.
void rotateToEarthRelative(WindField& w);

// The meridian-convergence angle (radians) at grid index (i, j): the bearing of
// the grid's +y (row) axis measured east of true north. Zero for lat/lon grids.
[[nodiscard]] double gridNorthAngle(const core::GridDef& grid, double i, double j);

// m/s -> knots.
[[nodiscard]] inline double toKnots(double ms) { return ms * 1.9438444924406; }

}  // namespace met::analysis
