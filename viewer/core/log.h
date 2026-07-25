#pragma once

#include <string>
#include <string_view>

#include <fmt/format.h>

namespace met::core {

// A minimal severity-filtered logger writing to stderr.
//
// Deliberately not spdlog: the only thing the app needs is a diagnostic channel
// for decode/IO failures that would otherwise be swallowed by a `catch (...)`,
// and fmt (already a dependency for time/level formatting) covers the formatting
// half of that. Keeping it dependency-free also keeps met_core free of anything
// the readers/analysis layers cannot use headlessly.
//
// Thread-safety: level() is an atomic read and each record is written with a
// single fwrite, so records from worker threads interleave but never tear.
enum class LogLevel { Trace, Debug, Info, Warn, Error, Off };

// Current threshold; records below it are dropped. Defaults to Warn so a normal
// run is quiet and a real failure is still visible on stderr.
[[nodiscard]] LogLevel logLevel();
void setLogLevel(LogLevel level);

// Parse "trace"/"debug"/"info"/"warn"/"error"/"off" (case-insensitive).
// Returns Info for an unrecognized string so `--verbose garbage` still talks.
[[nodiscard]] LogLevel parseLogLevel(std::string_view name);

// Emit one already-formatted record. Prefer the MET_LOG_* macros below.
void logRecord(LogLevel level, std::string_view message);

template <typename... Args>
void logf(LogLevel level, fmt::format_string<Args...> format, Args&&... args) {
    if (level < logLevel()) return;  // cheap early-out on the hot path
    logRecord(level, fmt::format(format, std::forward<Args>(args)...));
}

}  // namespace met::core

// The level check happens inside logf() too, but repeating it here keeps the
// argument evaluation (string building, what() calls) out of a disabled path.
#define MET_LOG(level, ...)                             \
    do {                                                \
        if ((level) >= ::met::core::logLevel())         \
            ::met::core::logf((level), __VA_ARGS__);    \
    } while (false)

#define MET_LOG_TRACE(...) MET_LOG(::met::core::LogLevel::Trace, __VA_ARGS__)
#define MET_LOG_DEBUG(...) MET_LOG(::met::core::LogLevel::Debug, __VA_ARGS__)
#define MET_LOG_INFO(...) MET_LOG(::met::core::LogLevel::Info, __VA_ARGS__)
#define MET_LOG_WARN(...) MET_LOG(::met::core::LogLevel::Warn, __VA_ARGS__)
#define MET_LOG_ERROR(...) MET_LOG(::met::core::LogLevel::Error, __VA_ARGS__)
