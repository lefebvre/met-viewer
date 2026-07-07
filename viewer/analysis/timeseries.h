#pragma once

#include <string>
#include <utility>
#include <vector>

#include "viewer/core/field.h"
#include "viewer/core/geo.h"
#include "viewer/core/timeaxis.h"

namespace met::analysis {

// A time series of a variable at a fixed point.
struct TimeSeries {
    core::LatLon point;
    std::vector<core::TimePoint> times;
    std::vector<float> values;  // NaN where off-grid/missing
    std::string units;
};

// Sample each (time, field) in `timeStack` at `point`.
[[nodiscard]] TimeSeries extractTimeSeries(
    const std::vector<std::pair<core::TimePoint, core::Field2D>>& timeStack, core::LatLon point);

}  // namespace met::analysis
