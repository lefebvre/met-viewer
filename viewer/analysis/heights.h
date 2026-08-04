#pragma once

#include <optional>
#include <string>
#include <vector>

#include "viewer/core/catalog.h"

namespace met::analysis {

// The variable that carries geopotential height, picked out of a dataset's
// catalog so soundings and cross-sections can label their pressure axis with an
// altitude. Recognizes geopotential height (`gh`, `hgt`, `zg`, standard name
// `geopotential_height`) and geopotential (`z`, m2/s2), preferring the former
// when both are present — the latter is only a division by g away, but reading
// the height straight out of the file avoids the conversion entirely.
//
// Candidates must carry height-compatible units (gpm/dam/m/m2/s2) and at least
// two levels: a single-level height field is a surface elevation, not a profile.
// Returns nullopt when the dataset has nothing to place levels with, which is
// the normal case for a temperature-only file.
[[nodiscard]] std::optional<std::string> findHeightVariable(
    const std::vector<core::VariableEntry>& vars);

}  // namespace met::analysis
