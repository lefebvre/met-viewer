#include "viewer/analysis/sounding.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "viewer/analysis/sample.h"
#include "viewer/analysis/wind.h"

namespace met::analysis {
namespace {
// The field in `stack` whose pressure matches `pressure`, or null if none.
const core::Field2D* fieldAtPressure(
    const std::vector<std::pair<double, core::Field2D>>& stack, double pressure) {
    for (const auto& [p, f] : stack)
        if (std::abs(p - pressure) < 1e-6) return &f;
    return nullptr;
}
}  // namespace

float dewpointFromRH(float tempK, float rhPercent) {
    if (std::isnan(tempK) || std::isnan(rhPercent) || rhPercent <= 0.0f)
        return std::numeric_limits<float>::quiet_NaN();
    const double b = 17.625, c = 243.04;  // Magnus coefficients (°C)
    const double tC = tempK - 273.15;
    const double rh = std::min(100.0, static_cast<double>(rhPercent));
    const double gamma = std::log(rh / 100.0) + (b * tC) / (c + tC);
    const double tdC = c * gamma / (b - gamma);
    return static_cast<float>(tdC + 273.15);
}

Sounding extractSounding(const std::vector<std::pair<double, core::Field2D>>& tStack,
                         const std::vector<std::pair<double, core::Field2D>>& rhStack,
                         core::LatLon point,
                         const std::vector<std::pair<double, core::Field2D>>& uStack,
                         const std::vector<std::pair<double, core::Field2D>>& vStack) {
    Sounding s;
    s.point = point;
    for (const auto& [pressure, tfield] : tStack) {
        SoundingLevel lvl;
        lvl.pressure = pressure;
        lvl.tempK = sampleBilinear(tfield, point);
        lvl.dewpointK = std::numeric_limits<float>::quiet_NaN();
        // Dewpoint from matching relative humidity at this pressure.
        if (const core::Field2D* rfield = fieldAtPressure(rhStack, pressure)) {
            const float rh = sampleBilinear(*rfield, point);
            lvl.dewpointK = dewpointFromRH(lvl.tempK, rh);
        }
        // Earth-relative wind from the matching U/V pair at this pressure. Building a
        // WindField lets rotateToEarthRelative fix up grid-relative components on a
        // projected grid (a no-op for regular lat/lon grids).
        const core::Field2D* ufield = fieldAtPressure(uStack, pressure);
        const core::Field2D* vfield = fieldAtPressure(vStack, pressure);
        if (ufield && vfield) {
            WindField w;
            w.u = *ufield;
            w.v = *vfield;
            rotateToEarthRelative(w);
            const UV uv = sampleWindLatLon(w, point);
            lvl.windU = uv.u;
            lvl.windV = uv.v;
        }
        s.levels.push_back(lvl);
    }
    // Sort top (low pressure) to bottom (high pressure).
    std::sort(s.levels.begin(), s.levels.end(),
              [](const SoundingLevel& a, const SoundingLevel& b) { return a.pressure < b.pressure; });
    return s;
}

}  // namespace met::analysis
