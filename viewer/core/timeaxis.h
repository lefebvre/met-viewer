#pragma once

#include <cstdint>
#include <string>

namespace met::core {

// A point in time as seconds since the Unix epoch (UTC). Meteorological data is
// always UTC; we keep a plain integer to stay trivially comparable/sortable and
// free of timezone handling.
struct TimePoint {
    std::int64_t epochSeconds = 0;

    friend bool operator==(TimePoint a, TimePoint b) { return a.epochSeconds == b.epochSeconds; }
    friend bool operator<(TimePoint a, TimePoint b) { return a.epochSeconds < b.epochSeconds; }
};

// ISO-8601 UTC string, e.g. "2024-05-01T12:00Z". For display only.
[[nodiscard]] std::string formatTime(TimePoint t);

// Portable UTC calendar -> Unix epoch seconds (proleptic Gregorian, no timezone
// or DST). A dependency-free replacement for the non-standard timegm().
[[nodiscard]] std::int64_t timegmUtc(int year, int mon, int day, int hour, int min, int sec);

// A vertical level. `value` is interpreted per `type` (hPa for pressure, metres
// for height, dimensionless index for model levels, etc.).
struct VerticalLevel {
    enum class Type {
        Surface,
        PressureHPa,
        HeightM,
        ModelLevel,
        Sigma,
        Hybrid,
        Isentropic,
        Unknown,
    };

    Type type = Type::Unknown;
    double value = 0.0;

    friend bool operator==(const VerticalLevel& a, const VerticalLevel& b) {
        return a.type == b.type && a.value == b.value;
    }
};

// Short, sortable ordering key so a variable's levels present top-of-atmosphere
// to surface in a consistent way. Surface sorts last.
[[nodiscard]] double levelSortKey(const VerticalLevel& lvl);

// Human-readable label, e.g. "500 hPa", "10 m", "surface".
[[nodiscard]] std::string formatLevel(const VerticalLevel& lvl);

}  // namespace met::core
