#include "viewer/analysis/sounding.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "viewer/analysis/sample.h"
#include "viewer/analysis/wind.h"
#include "viewer/core/units.h"

namespace met::analysis {
namespace {
// The field in `stack` whose pressure matches `pressure`, or null if none.
const core::Field2D* fieldAtPressure(
    const std::vector<std::pair<double, core::Field2D>>& stack, double pressure) {
    for (const auto& [p, f] : stack)
        if (std::abs(p - pressure) < 1e-6) return &f;
    return nullptr;
}

// The field in `stack` whose key (model-level index) matches `key`, or null.
const core::Field2D* fieldAtKey(
    const std::vector<std::pair<double, core::Field2D>>& stack, double key) {
    for (const auto& [k, f] : stack)
        if (std::abs(k - key) < 1e-6) return &f;
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

float dewpointFromSpecificHumidity(float qKgKg, double pressureHpa, float tempK) {
    if (std::isnan(qKgKg) || qKgKg <= 0.0f || !std::isfinite(pressureHpa) || pressureHpa <= 0.0)
        return std::numeric_limits<float>::quiet_NaN();
    const double b = 17.625, c = 243.04, es0 = 6.112;  // Magnus (°C, hPa)
    const double q = qKgKg;
    // Vapour pressure (hPa) from specific humidity and pressure.
    const double e = q * pressureHpa / (0.622 + 0.378 * q);
    if (e <= 0.0) return std::numeric_limits<float>::quiet_NaN();
    const double ln = std::log(e / es0);
    const double tdC = c * ln / (b - ln);
    double tdK = tdC + 273.15;
    if (!std::isnan(tempK)) tdK = std::min(tdK, static_cast<double>(tempK));  // Td never exceeds T
    return static_cast<float>(tdK);
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
        // Earth-relative wind from the matching U/V pair at this pressure. Rotating
        // only the sampled vector (not the whole grid) keeps this O(1) per level.
        const core::Field2D* ufield = fieldAtPressure(uStack, pressure);
        const core::Field2D* vfield = fieldAtPressure(vStack, pressure);
        if (ufield && vfield) {
            const UV uv = earthRelativeWindAt(*ufield, *vfield, point);
            lvl.windU = uv.u;
            lvl.windV = uv.v;
        }
        s.levels.push_back(std::move(lvl));
    }
    // Sort top (low pressure) to bottom (high pressure).
    std::sort(s.levels.begin(), s.levels.end(),
              [](const SoundingLevel& a, const SoundingLevel& b) { return a.pressure < b.pressure; });
    return s;
}

Sounding extractSoundingModelLevels(
    const std::vector<std::pair<double, core::Field2D>>& tStack,
    const std::vector<std::pair<double, core::Field2D>>& presStack,
    const std::vector<std::pair<double, core::Field2D>>& qStack, core::LatLon point,
    const std::vector<std::pair<double, core::Field2D>>& uStack,
    const std::vector<std::pair<double, core::Field2D>>& vStack) {
    Sounding s;
    s.point = point;
    for (const auto& [levelKey, tfield] : tStack) {
        const core::Field2D* pfield = fieldAtKey(presStack, levelKey);
        if (!pfield) continue;  // no pressure at this level -> cannot place it
        const float rawP = sampleBilinear(*pfield, point);
        if (std::isnan(rawP)) continue;
        SoundingLevel lvl;
        lvl.pressure = core::toHpa(rawP, pfield->meta.units);
        lvl.tempK = sampleBilinear(tfield, point);
        lvl.dewpointK = std::numeric_limits<float>::quiet_NaN();
        if (const core::Field2D* qf = fieldAtKey(qStack, levelKey)) {
            const float q = sampleBilinear(*qf, point);
            lvl.dewpointK = dewpointFromSpecificHumidity(q, lvl.pressure, lvl.tempK);
        }
        const core::Field2D* ufield = fieldAtKey(uStack, levelKey);
        const core::Field2D* vfield = fieldAtKey(vStack, levelKey);
        if (ufield && vfield) {
            const UV uv = earthRelativeWindAt(*ufield, *vfield, point);
            lvl.windU = uv.u;
            lvl.windV = uv.v;
        }
        s.levels.push_back(lvl);
    }
    std::sort(s.levels.begin(), s.levels.end(),
              [](const SoundingLevel& a, const SoundingLevel& b) { return a.pressure < b.pressure; });
    return s;
}

}  // namespace met::analysis
