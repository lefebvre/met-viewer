#pragma once

#include <optional>

#include "viewer/analysis/wind.h"
#include "viewer/core/field.h"

namespace met::analysis {

// Wind speed and meteorological direction (degrees, the direction the wind blows
// FROM, 0 = from north, 90 = from east) from one earth-relative vector. NaN if
// either component is NaN.
//
// The field versions below call these, so a single sampled value -- a profile
// table cell, a cursor readout -- and a rendered field cannot disagree about the
// convention. Getting the direction's sign or offset wrong is invisible in a
// contour plot and glaring in a table of numbers, which is exactly the kind of
// drift a shared formula prevents.
[[nodiscard]] float windSpeedFrom(float u, float v);
[[nodiscard]] float windDirectionFrom(float u, float v);

// Scalar wind speed field, sqrt(u^2 + v^2), from a paired wind field.
[[nodiscard]] core::Field2D windSpeedField(const WindField& w);

// Meteorological wind direction (degrees, the direction the wind blows FROM,
// 0 = from north, 90 = from east) from a paired wind field.
[[nodiscard]] core::Field2D windDirectionField(const WindField& w);

// Potential temperature theta = T * (1000/p)^0.2854 (K), from a temperature
// field (K) at pressure `pressureHPa`.
[[nodiscard]] core::Field2D potentialTemperatureField(const core::Field2D& tempK,
                                                      double pressureHPa);

// If `f` is a temperature field (recognized by standard name, variable name, or
// units of K/°C), return a copy with values in Kelvin; otherwise std::nullopt.
// Guards potential-temperature computation against non-temperature or
// wrong-unit input.
[[nodiscard]] std::optional<core::Field2D> asTemperatureKelvin(const core::Field2D& f);

// Relative vorticity zeta = dv/dx - du/dy (1/s), by centered finite differences.
// Grid spacing is handled per grid type: metres directly for projected grids,
// and R*cos(lat)*dlon / R*dlat for regular lat/lon grids.
//
// The derivatives are taken along grid rows and columns, so they are computed
// from components resolved on those axes. Earth-relative components on a
// projected grid (meta.gridRelativeWind false) are rotated back to the grid frame
// on a copy first: differencing them directly gives cos θ·ζ + sin θ·δ for a
// local meridian convergence θ, and on HRRR's western edge (θ ≈ 16°) that puts a
// quarter of the divergence into the vorticity. Passing the components as
// decoded skips the copy. The map factor of a conformal projection (a few
// percent over a CONUS domain) is not applied.
[[nodiscard]] core::Field2D relativeVorticityField(const WindField& w);

// Horizontal divergence du/dx + dv/dy (1/s), same differencing and frame
// handling as vorticity.
[[nodiscard]] core::Field2D divergenceField(const WindField& w);

}  // namespace met::analysis
