#include "viewer/analysis/crosssection.h"

#include <algorithm>
#include <cstddef>
#include <vector>

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

    // Sort by pressure ascending (top of atmosphere first) so the pressure axis
    // is monotonic regardless of the caller's ordering — the view's log-p level
    // mapping assumes a sorted axis.
    std::vector<std::size_t> order(levelStack.size());
    for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::sort(order.begin(), order.end(),
              [&](std::size_t a, std::size_t b) { return levelStack[a].first < levelStack[b].first; });

    cs.pressures.reserve(levelStack.size());
    cs.values.reserve(levelStack.size());
    for (std::size_t idx : order) {
        const double pressure = levelStack[idx].first;
        const core::Field2D& field = levelStack[idx].second;
        cs.pressures.push_back(pressure);
        std::vector<float> row;
        row.reserve(cs.points.size());
        for (const core::LatLon& p : cs.points) row.push_back(sampleBilinear(field, p));
        cs.values.push_back(std::move(row));
    }
    return cs;
}

}  // namespace met::analysis
