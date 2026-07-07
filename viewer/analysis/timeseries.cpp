#include "viewer/analysis/timeseries.h"

#include "viewer/analysis/sample.h"

namespace met::analysis {

TimeSeries extractTimeSeries(
    const std::vector<std::pair<core::TimePoint, core::Field2D>>& timeStack, core::LatLon point) {
    TimeSeries ts;
    ts.point = point;
    if (!timeStack.empty() && !timeStack.front().second.meta.units.empty())
        ts.units = timeStack.front().second.meta.units;
    for (const auto& [time, field] : timeStack) {
        ts.times.push_back(time);
        ts.values.push_back(sampleBilinear(field, point));
    }
    return ts;
}

}  // namespace met::analysis
