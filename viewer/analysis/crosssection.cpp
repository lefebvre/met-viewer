#include "viewer/analysis/crosssection.h"

#include "viewer/analysis/sample.h"

namespace met::analysis {

CrossSection extractCrossSection(
    const std::vector<std::pair<double, core::Field2D>>& levelStack,
    const std::vector<core::LatLon>& vertices, int nSamples) {
    CrossSection cs;
    if (levelStack.empty() || vertices.size() < 2 || nSamples < 2) return cs;

    const core::SampledPath path = core::sampleGreatCirclePath(vertices, nSamples);
    cs.points = path.points;
    cs.distancesKm = path.distancesKm;
    if (!levelStack.front().second.meta.units.empty())
        cs.units = levelStack.front().second.meta.units;

    cs.pressures.reserve(levelStack.size());
    cs.values.reserve(levelStack.size());
    for (const auto& [pressure, field] : levelStack) {
        cs.pressures.push_back(pressure);
        std::vector<float> row;
        row.reserve(cs.points.size());
        for (const core::LatLon& p : cs.points) row.push_back(sampleBilinear(field, p));
        cs.values.push_back(std::move(row));
    }
    return cs;
}

}  // namespace met::analysis
