#include "viewer/analysis/timeseries.h"

#include "viewer/analysis/sample.h"

namespace met::analysis {

TimeSeries extractTimeSeries(
    const std::vector<std::pair<core::TimePoint, core::Field2D>>& timeStack, core::LatLon point) {
    TimeSeries ts;
    ts.point = point;
    if (!timeStack.empty()) {
        const core::FieldMeta& meta = timeStack.front().second.meta;
        if (!meta.units.empty()) ts.units = meta.units;
        ts.varName = meta.varName;
        ts.level = meta.level;
        ts.member = meta.member;
    }
    for (const auto& [time, field] : timeStack) {
        ts.times.push_back(time);
        ts.values.push_back(sampleBilinear(field, point));
    }
    return ts;
}

}  // namespace met::analysis
