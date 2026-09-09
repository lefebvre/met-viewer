#include "viewer/analysis/pointprofile.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

#include "viewer/analysis/derived.h"
#include "viewer/analysis/heights.h"
#include "viewer/analysis/sample.h"
#include "viewer/analysis/wind.h"
#include "viewer/core/grid.h"
#include "viewer/core/units.h"

namespace met::analysis {
namespace {

constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();

// Stack keys are floating point (a pressure in hPa, or a model-level index that
// arrived as a double), so they are matched with the same tolerance sounding.cpp
// uses rather than by equality.
constexpr double kKeyTolerance = 1e-6;

[[nodiscard]] const core::Field2D* fieldAtKey(const ProfileStack& stack, double key) {
    for (const auto& [k, f] : stack)
        if (std::abs(k - key) < kKeyTolerance) return &f;
    return nullptr;
}

// One sample, with the three-way answer that makes CellStatus worth having.
// `value` is left untouched unless the read succeeds.
[[nodiscard]] CellStatus sampleCell(const core::Field2D& field, core::LatLon at, bool& anyInDomain,
                                    float& value) {
    // latlonToIndex is O(1) and is the only thing that can tell an off-domain
    // point from missing data -- sampleBilinear returns NaN for both.
    if (!core::latlonToIndex(field.grid, at).inDomain) return CellStatus::OutsideGrid;
    anyInDomain = true;
    const float v = sampleBilinear(field, at);
    if (std::isnan(v)) return CellStatus::Missing;
    value = v;
    return CellStatus::Ok;
}

// How many levels of `type` a variable carries.
[[nodiscard]] std::size_t countLevelsOfType(const core::VariableEntry& v,
                                            core::VerticalLevel::Type type) {
    std::size_t n = 0;
    for (const core::VerticalLevel& lvl : v.levels)
        if (lvl.type == type) ++n;
    return n;
}

// A variable needs two levels of the axis to be a profile column; see the header.
constexpr std::size_t kMinProfileLevels = 2;

[[nodiscard]] ProfileColumn columnFor(const core::VariableEntry& v) {
    return ProfileColumn{v.varName, v.longName.empty() ? v.varName : v.longName, v.units,
                         ProfileColumnKind::Variable};
}

}  // namespace

ProfileColumnChoices profileColumnChoices(const std::vector<core::VariableEntry>& vars) {
    using T = core::VerticalLevel::Type;
    ProfileColumnChoices out;

    // Priority order, matching how computeSounding picks its path: isobaric first
    // because that is what most distributed products carry, then the native model
    // axes, then height levels. The first axis any variable has a real profile on
    // wins -- mixing two axes into one table would put rows on it that do not
    // share a vertical coordinate.
    for (const T candidate :
         {T::PressureHPa, T::Hybrid, T::Sigma, T::ModelLevel, T::HeightM, T::Isentropic}) {
        const bool anyProfile = std::any_of(vars.begin(), vars.end(), [candidate](const auto& v) {
            return countLevelsOfType(v, candidate) >= kMinProfileLevels;
        });
        if (anyProfile) {
            out.levelType = candidate;
            break;
        }
    }
    if (out.levelType == T::Unknown) return out;

    const std::optional<std::string> heightVar = findHeightVariable(vars);
    if (heightVar) out.heightVar = *heightVar;

    std::vector<core::VerticalLevel> levels;
    for (const core::VariableEntry& v : vars) {
        if (countLevelsOfType(v, out.levelType) < kMinProfileLevels) {
            out.unavailable.push_back(columnFor(v));
            continue;
        }
        for (const core::VerticalLevel& lvl : v.levels)
            if (lvl.type == out.levelType) levels.push_back(lvl);
        // The height field is already the profile's altitude coordinate; offering
        // it again as a column would print the same number twice.
        if (heightVar && v.varName == *heightVar) continue;
        out.available.push_back(columnFor(v));
    }

    // The row set is the union across every qualifying variable, so a column that
    // is missing one level reads NoRecord there rather than shortening the table.
    std::sort(levels.begin(), levels.end(), [](const auto& a, const auto& b) {
        return core::levelSortKey(a) > core::levelSortKey(b);  // ground first
    });
    levels.erase(std::unique(levels.begin(), levels.end()), levels.end());
    out.levels = std::move(levels);

    std::vector<std::string> names;
    names.reserve(out.available.size());
    for (const ProfileColumn& c : out.available) names.push_back(c.id);
    out.windAvailable = findWindPair(names).has_value();

    return out;
}

PointProfile makeProfile(const std::vector<core::VerticalLevel>& levels,
                         const std::vector<ProfileColumn>& columns, core::LatLon point) {
    PointProfile p;
    p.point = point;
    p.columns = columns;
    if (!levels.empty()) p.levelType = levels.front().type;

    std::vector<core::VerticalLevel> sorted = levels;
    std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) {
        return core::levelSortKey(a) > core::levelSortKey(b);  // ground first; see the header
    });

    p.levels.reserve(sorted.size());
    for (const core::VerticalLevel& lvl : sorted) {
        ProfileLevel row;
        row.level = lvl;
        // On an isobaric axis the level *is* the pressure, so there is nothing to
        // read for it. Every other axis needs fillProfilePressures.
        if (lvl.type == core::VerticalLevel::Type::PressureHPa) {
            row.pressureHpa = lvl.value;
            row.pressureStatus = CellStatus::Ok;
        }
        row.values.assign(columns.size(), kNaN);
        row.statuses.assign(columns.size(), CellStatus::NoRecord);
        p.levels.push_back(std::move(row));
    }
    return p;
}

void fillProfileColumn(PointProfile& p, std::size_t columnIndex, const ProfileStack& stack) {
    if (columnIndex >= p.columns.size()) return;
    for (ProfileLevel& row : p.levels) {
        const core::Field2D* f = fieldAtKey(stack, row.level.value);
        if (!f) continue;  // stays NoRecord
        row.statuses[columnIndex] =
            sampleCell(*f, p.point, p.pointInDomain, row.values[columnIndex]);
    }
}

void fillProfileWind(PointProfile& p, const ProfileStack& u, const ProfileStack& v) {
    for (std::size_t c = 0; c < p.columns.size(); ++c) {
        const ProfileColumnKind kind = p.columns[c].kind;
        if (kind != ProfileColumnKind::WindSpeed && kind != ProfileColumnKind::WindDirection)
            continue;

        for (ProfileLevel& row : p.levels) {
            const core::Field2D* uf = fieldAtKey(u, row.level.value);
            const core::Field2D* vf = fieldAtKey(v, row.level.value);
            if (!uf || !vf) continue;  // a component missing here is no wind here
            if (!core::latlonToIndex(uf->grid, p.point).inDomain) {
                row.statuses[c] = CellStatus::OutsideGrid;
                continue;
            }
            p.pointInDomain = true;
            // Rotates the single sampled vector where the components are
            // grid-relative on a projected grid; taking hypot/atan2 of the raw
            // values would give a direction off by the meridian convergence.
            const UV uv = earthRelativeWindAt(*uf, *vf, p.point);
            const float value = kind == ProfileColumnKind::WindSpeed
                                    ? windSpeedFrom(uv.u, uv.v)
                                    : windDirectionFrom(uv.u, uv.v);
            if (std::isnan(value)) {
                row.statuses[c] = CellStatus::Missing;
                continue;
            }
            row.values[c] = value;
            row.statuses[c] = CellStatus::Ok;
        }
    }
}

void fillProfileHeights(PointProfile& p, const ProfileStack& z, const std::string& sourceVar) {
    p.heightSourceVar = sourceVar;
    for (ProfileLevel& row : p.levels) {
        const core::Field2D* f = fieldAtKey(z, row.level.value);
        if (!f) continue;
        float raw = kNaN;
        const CellStatus st = sampleCell(*f, p.point, p.pointInDomain, raw);
        if (st != CellStatus::Ok) {
            row.heightStatus = st;
            continue;
        }
        const double gpm = core::toGeopotentialMeters(static_cast<double>(raw), f->meta.units);
        if (std::isnan(gpm)) {
            // toGeopotentialMeters refuses to guess at unplaceable units, and a
            // blank altitude is the honest result -- 5000 is plausible in gpm and
            // in m2/s2 alike, so a guess here would be a wrong altitude.
            row.heightStatus = CellStatus::Unconvertible;
            continue;
        }
        row.heightGpm = static_cast<float>(gpm);
        row.heightStatus = CellStatus::Ok;
    }
}

void fillProfilePressures(PointProfile& p, const ProfileStack& pres) {
    for (ProfileLevel& row : p.levels) {
        const core::Field2D* f = fieldAtKey(pres, row.level.value);
        if (!f) continue;
        float raw = kNaN;
        const CellStatus st = sampleCell(*f, p.point, p.pointInDomain, raw);
        if (st != CellStatus::Ok) {
            row.pressureStatus = st;
            continue;
        }
        row.pressureHpa = core::toHpa(static_cast<double>(raw), f->meta.units);
        row.pressureStatus = CellStatus::Ok;
    }
}

}  // namespace met::analysis
