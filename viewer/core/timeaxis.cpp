#include "viewer/core/timeaxis.h"

#include <fmt/format.h>

namespace met::core {
namespace {

// Days since 1970-01-01 for a civil date (proleptic Gregorian), and its inverse.
// Howard Hinnant's algorithms — branch-free, valid for the full int64 range and
// independent of the platform timezone database.
std::int64_t daysFromCivil(std::int64_t y, unsigned m, unsigned d) {
    y -= m <= 2;
    const std::int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned mp = m > 2 ? m - 3 : m + 9;  // months since March, avoids a signed literal
    const unsigned doy = (153 * mp + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<std::int64_t>(doe) - 719468;
}

void civilFromDays(std::int64_t z, int& y, unsigned& m, unsigned& d) {
    z += 719468;
    const std::int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    d = doy - (153 * mp + 2) / 5 + 1;
    m = mp < 10 ? mp + 3 : mp - 9;
    y = static_cast<int>(static_cast<std::int64_t>(yoe) + era * 400 + (m <= 2));
}

}  // namespace

std::int64_t timegmUtc(int year, int mon, int day, int hour, int min, int sec) {
    const std::int64_t days = daysFromCivil(year, static_cast<unsigned>(mon), static_cast<unsigned>(day));
    return ((days * 24 + hour) * 60 + min) * 60 + sec;
}

std::string formatTime(TimePoint t) {
    const std::int64_t s = t.epochSeconds;
    const std::int64_t days = (s >= 0 ? s : s - 86399) / 86400;  // floor division
    const std::int64_t rem = s - days * 86400;                   // [0, 86400)
    int y = 0;
    unsigned mo = 0, d = 0;
    civilFromDays(days, y, mo, d);
    const int hh = static_cast<int>(rem / 3600);
    const int mm = static_cast<int>((rem % 3600) / 60);
    return fmt::format("{:04d}-{:02d}-{:02d}T{:02d}:{:02d}Z", y, mo, d, hh, mm);
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
