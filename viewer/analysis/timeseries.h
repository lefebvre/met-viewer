#pragma once

#include <string>
#include <utility>
#include <vector>

#include "viewer/core/field.h"
#include "viewer/core/geo.h"
#include "viewer/core/timeaxis.h"

namespace met::analysis {

// A time series of a variable at a fixed point. `varName`, `level` and `member`
// are those of the fields it was sampled from.
struct TimeSeries {
    core::LatLon point;
    std::string varName;
    core::VerticalLevel level;
    int member = -1;  // ensemble member, -1 = deterministic
    std::vector<core::TimePoint> times;
    std::vector<float> values;  // NaN where off-grid/missing
    std::string units;
};

// Sample each (time, field) in `timeStack` at `point`.
[[nodiscard]] TimeSeries extractTimeSeries(
    const std::vector<std::pair<core::TimePoint, core::Field2D>>& timeStack, core::LatLon point);

}  // namespace met::analysis
