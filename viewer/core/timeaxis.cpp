#include "viewer/core/timeaxis.h"

#include <ctime>

#include <fmt/format.h>

namespace met::core {

std::string formatTime(TimePoint t) {
    const std::time_t secs = static_cast<std::time_t>(t.epochSeconds);
    std::tm tm{};
    gmtime_r(&secs, &tm);
    return fmt::format("{:04d}-{:02d}-{:02d}T{:02d}:{:02d}Z", tm.tm_year + 1900, tm.tm_mon + 1,
                       tm.tm_mday, tm.tm_hour, tm.tm_min);
}

double levelSortKey(const VerticalLevel& lvl) {
    using T = VerticalLevel::Type;
    switch (lvl.type) {
        // Pressure decreases with height: smaller hPa is higher up. Present
        // high-to-low altitude, i.e. ascending pressure -> use +value.
        case T::PressureHPa:
            return lvl.value;
        // Height/isentropic increase with altitude; negate so higher sorts first.
        case T::HeightM:
        case T::Isentropic:
            return -lvl.value;
        case T::ModelLevel:
        case T::Sigma:
        case T::Hybrid:
            return lvl.value;
        case T::Surface:
        case T::Unknown:
            break;
    }
    return 1e12;  // surface / unknown sort last
}

std::string formatLevel(const VerticalLevel& lvl) {
    using T = VerticalLevel::Type;
    switch (lvl.type) {
        case T::Surface:
            return "surface";
        case T::PressureHPa:
            return fmt::format("{:g} hPa", lvl.value);
        case T::HeightM:
            return fmt::format("{:g} m", lvl.value);
        case T::ModelLevel:
            return fmt::format("model level {:g}", lvl.value);
        case T::Sigma:
            return fmt::format("sigma {:g}", lvl.value);
        case T::Hybrid:
            return fmt::format("hybrid {:g}", lvl.value);
        case T::Isentropic:
            return fmt::format("{:g} K", lvl.value);
        case T::Unknown:
            break;
    }
    return fmt::format("level {:g}", lvl.value);
}

}  // namespace met::core
