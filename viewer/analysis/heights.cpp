#include "viewer/analysis/heights.h"

#include <algorithm>
#include <cctype>
#include <cmath>

#include "viewer/core/units.h"

namespace met::analysis {
namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// Whether a sample in these units can be placed on a geopotential-height axis.
bool heightUnits(const std::string& units) {
    return std::isfinite(core::toGeopotentialMeters(1.0, units));
}

// How good a height variable this is; 0 means "not one". Name/standard-name
// matching only — units alone would also accept cloud-top height, snow depth and
// every other length-valued field in the file.
int heightScore(const core::VariableEntry& v) {
    const std::string sn = lower(v.standardName);
    const std::string nm = lower(v.varName);
    if (sn == "geopotential_height") return 100;
    if (nm == "gh" || nm == "hgt" || nm == "hgts" || nm == "zg") return 90;
    if (sn == "geopotential" || nm == "z") return 80;
    return 0;
}

}  // namespace

std::optional<std::string> findHeightVariable(const std::vector<core::VariableEntry>& vars) {
    const core::VariableEntry* best = nullptr;
    int bestScore = 0;
    for (const auto& v : vars) {
        if (v.levels.size() < 2) continue;  // a one-level height is terrain, not a profile
        if (!heightUnits(v.units)) continue;
        const int score = heightScore(v);
        if (score > bestScore) {
            bestScore = score;
            best = &v;
        }
    }
    if (!best) return std::nullopt;
    return best->varName;
}

}  // namespace met::analysis
