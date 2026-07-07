#include "viewer/analysis/sounding.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "viewer/analysis/sample.h"

namespace met::analysis {

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
                         core::LatLon point) {
    Sounding s;
    s.point = point;
    for (const auto& [pressure, tfield] : tStack) {
        SoundingLevel lvl;
        lvl.pressure = pressure;
        lvl.tempK = sampleBilinear(tfield, point);
        lvl.dewpointK = std::numeric_limits<float>::quiet_NaN();
        // Find matching RH at this pressure.
        for (const auto& [rp, rfield] : rhStack) {
            if (std::abs(rp - pressure) < 1e-6) {
                const float rh = sampleBilinear(rfield, point);
                lvl.dewpointK = dewpointFromRH(lvl.tempK, rh);
                break;
            }
        }
        s.levels.push_back(lvl);
    }
    // Sort top (low pressure) to bottom (high pressure).
    std::sort(s.levels.begin(), s.levels.end(),
              [](const SoundingLevel& a, const SoundingLevel& b) { return a.pressure < b.pressure; });
    return s;
}

}  // namespace met::analysis
