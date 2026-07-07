#pragma once

#include "viewer/analysis/wind.h"
#include "viewer/core/field.h"

namespace met::analysis {

// Scalar wind speed field, sqrt(u^2 + v^2), from a paired wind field.
[[nodiscard]] core::Field2D windSpeedField(const WindField& w);

// Meteorological wind direction (degrees, the direction the wind blows FROM,
// 0 = from north, 90 = from east) from a paired wind field.
[[nodiscard]] core::Field2D windDirectionField(const WindField& w);

// Potential temperature theta = T * (1000/p)^0.2854 (K), from a temperature
// field (K) at pressure `pressureHPa`.
[[nodiscard]] core::Field2D potentialTemperatureField(const core::Field2D& tempK,
                                                      double pressureHPa);

// Relative vorticity zeta = dv/dx - du/dy (1/s), by centered finite differences.
// Grid spacing is handled per grid type: metres directly for projected grids,
// and R*cos(lat)*dlon / R*dlat for regular lat/lon grids.
[[nodiscard]] core::Field2D relativeVorticityField(const WindField& w);

// Horizontal divergence du/dx + dv/dy (1/s), same differencing as vorticity.
[[nodiscard]] core::Field2D divergenceField(const WindField& w);

}  // namespace met::analysis
