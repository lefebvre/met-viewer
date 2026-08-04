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

// The field in `stack` whose key matches `key`, or null if none. Keys are level
// pressures on the isobaric path and model-level indices on the native path.
const core::Field2D* fieldAtKey(const std::vector<std::pair<double, core::Field2D>>& stack,
                                double key) {
    for (const auto& [k, f] : stack)
        if (std::abs(k - key) < 1e-6) return &f;
    return nullptr;
}

// One row of geopotential heights (gpm) sampled along the path, converted through
// the field's own units. All-NaN when the level has no height field.
std::vector<double> heightRow(const core::Field2D* zfield,
                              const std::vector<core::GridIndex>& pathIdx) {
    std::vector<double> row(pathIdx.size(), std::numeric_limits<double>::quiet_NaN());
    if (!zfield) return row;
    for (std::size_t i = 0; i < pathIdx.size(); ++i) {
        const float raw = sampleAt(*zfield, pathIdx[i]);
        if (!std::isnan(raw)) row[i] = core::toGeopotentialMeters(raw, zfield->meta.units);
    }
    return row;
}

// Drop a height field that came out entirely NaN, so views can treat "no heights"
// as one condition (an empty vector) instead of two.
void dropIfAllNaN(std::vector<std::vector<double>>& heights) {
    for (const auto& row : heights)
        for (double h : row)
            if (std::isfinite(h)) return;
    heights.clear();
}

// Mean of the finite entries, or +inf if none (sorts such a level to the bottom).
double finiteMean(const std::vector<double>& v) {
    double sum = 0.0;
    int n = 0;
    for (double x : v)
        if (std::isfinite(x)) {
            sum += x;
            ++n;
        }
    return n ? sum / n : std::numeric_limits<double>::infinity();
}

}  // namespace

CrossSection extractCrossSection(const std::vector<std::pair<double, core::Field2D>>& levelStack,
                                 const std::vector<core::LatLon>& vertices, int nSamples,
                                 const std::vector<std::pair<double, core::Field2D>>& zStack) {
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
    std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        return levelStack[a].first < levelStack[b].first;
    });

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
        // The pressure is one number per level; the height of that surface is not,
        // so it is sampled column by column like the value itself.
        if (!zStack.empty()) cs.heights.push_back(heightRow(fieldAtKey(zStack, pressure), pathIdx));
    }
    dropIfAllNaN(cs.heights);
    return cs;
}

CrossSection extractCrossSectionModelLevels(
    const std::vector<std::pair<double, core::Field2D>>& levelStack,
    const std::vector<std::pair<double, core::Field2D>>& presStack,
    const std::vector<core::LatLon>& vertices, int nSamples,
    const std::vector<std::pair<double, core::Field2D>>& zStack) {
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
        std::vector<double> heights;
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
        const core::Field2D* pfield = fieldAtKey(presStack, levelKey);
        if (!pfield) continue;  // no pressure at this level

        Row row;
        row.values.reserve(cs.points.size());
        row.pressures.reserve(cs.points.size());
        for (const core::GridIndex& gi : pathIdx) {
            row.values.push_back(sampleAt(vfield, gi));
            const float rawP = sampleAt(*pfield, gi);
            row.pressures.push_back(std::isnan(rawP) ? std::numeric_limits<double>::quiet_NaN()
                                                     : core::toHpa(rawP, pfield->meta.units));
        }
        if (!zStack.empty()) row.heights = heightRow(fieldAtKey(zStack, levelKey), pathIdx);
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
        if (!zStack.empty()) cs.heights.push_back(std::move(r.heights));
    }
    dropIfAllNaN(cs.heights);
    return cs;
}

}  // namespace met::analysis
