#include "viewer/analysis/crosssection.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numeric>
#include <vector>

#include "viewer/analysis/sample.h"
#include "viewer/core/units.h"

namespace met::analysis {
namespace {

// Sample a field at a precomputed grid index (NaN when the point is off-domain).
// Lets the caller project the path once and reuse it across every level, since all
// levels of a variable share one grid.
float sampleAt(const core::Field2D& field, const core::GridIndex& gi) {
    return gi.inDomain ? sampleBilinearIndex(field, gi.x, gi.y)
                       : std::numeric_limits<float>::quiet_NaN();
}

// Mean of the finite entries, or +inf if none (sorts such a level to the bottom).
double finiteMean(const std::vector<double>& v) {
    double sum = 0.0;
    int n = 0;
    for (double x : v)
        if (std::isfinite(x)) { sum += x; ++n; }
    return n ? sum / n : std::numeric_limits<double>::infinity();
}

}  // namespace

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
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::sort(order.begin(), order.end(),
              [&](std::size_t a, std::size_t b) { return levelStack[a].first < levelStack[b].first; });

    // Project the path to grid indices once — every level shares the same grid.
    std::vector<core::GridIndex> pathIdx;
    pathIdx.reserve(cs.points.size());
    for (const core::LatLon& p : cs.points)
        pathIdx.push_back(core::latlonToIndex(levelStack.front().second.grid, p));

    cs.pressures.reserve(levelStack.size());
    cs.values.reserve(levelStack.size());
    for (std::size_t idx : order) {
        const double pressure = levelStack[idx].first;
        const core::Field2D& field = levelStack[idx].second;
        std::vector<float> row;
        row.reserve(cs.points.size());
        for (const core::GridIndex& gi : pathIdx) row.push_back(sampleAt(field, gi));
        cs.values.push_back(std::move(row));
        cs.pressures.emplace_back(cs.points.size(), pressure);  // broadcast isobaric level
    }
    return cs;
}

CrossSection extractCrossSectionModelLevels(
    const std::vector<std::pair<double, core::Field2D>>& levelStack,
    const std::vector<std::pair<double, core::Field2D>>& presStack,
    const std::vector<core::LatLon>& vertices, int nSamples) {
    CrossSection cs;
    if (levelStack.empty() || presStack.empty() || vertices.size() < 2 || nSamples < 2) return cs;

    const core::SampledPath path = core::sampleGreatCirclePath(vertices, nSamples);
    cs.points = path.points;
    cs.distancesKm = path.distancesKm;
    if (!levelStack.front().second.meta.units.empty())
        cs.units = levelStack.front().second.meta.units;

    // Sample value and pressure per level (keyed by model-level index), building a
    // per-column pressure profile, then order levels by their mean pressure.
    struct Row {
        std::vector<float> values;
        std::vector<double> pressures;
        double meanP;
    };
    // Project the path to grid indices once — every level shares the same grid.
    std::vector<core::GridIndex> pathIdx;
    pathIdx.reserve(cs.points.size());
    for (const core::LatLon& p : cs.points)
        pathIdx.push_back(core::latlonToIndex(levelStack.front().second.grid, p));

    std::vector<Row> rows;
    rows.reserve(levelStack.size());
    for (const auto& [levelKey, vfield] : levelStack) {
        const core::Field2D* pfield = nullptr;
        for (const auto& [k, f] : presStack)
            if (std::abs(k - levelKey) < 1e-6) { pfield = &f; break; }
        if (!pfield) continue;  // no pressure at this level

        Row row;
        row.values.reserve(cs.points.size());
        row.pressures.reserve(cs.points.size());
        for (const core::GridIndex& gi : pathIdx) {
            row.values.push_back(sampleAt(vfield, gi));
            const float rawP = sampleAt(*pfield, gi);
            row.pressures.push_back(std::isnan(rawP)
                                        ? std::numeric_limits<double>::quiet_NaN()
                                        : core::toHpa(rawP, pfield->meta.units));
        }
        row.meanP = finiteMean(row.pressures);
        rows.push_back(std::move(row));
    }

    std::sort(rows.begin(), rows.end(),
              [](const Row& a, const Row& b) { return a.meanP < b.meanP; });

    cs.values.reserve(rows.size());
    cs.pressures.reserve(rows.size());
    for (auto& r : rows) {
        cs.values.push_back(std::move(r.values));
        cs.pressures.push_back(std::move(r.pressures));
    }
    return cs;
}

}  // namespace met::analysis
