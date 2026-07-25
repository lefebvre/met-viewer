#include "viewer/core/log.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <string>

namespace met::core {
namespace {

std::atomic<LogLevel> g_level{LogLevel::Warn};

const char* levelTag(LogLevel level) {
    switch (level) {
        case LogLevel::Trace:
            return "trace";
        case LogLevel::Debug:
            return "debug";
        case LogLevel::Info:
            return "info";
        case LogLevel::Warn:
            return "warn";
        case LogLevel::Error:
            return "error";
        case LogLevel::Off:
            break;
    }
    return "off";
}

}  // namespace

LogLevel logLevel() { return g_level.load(std::memory_order_relaxed); }

void setLogLevel(LogLevel level) { g_level.store(level, std::memory_order_relaxed); }

LogLevel parseLogLevel(std::string_view name) {
    std::string n(name);
    std::transform(n.begin(), n.end(), n.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (n == "trace") return LogLevel::Trace;
    if (n == "debug") return LogLevel::Debug;
    if (n == "info") return LogLevel::Info;
    if (n == "warn" || n == "warning") return LogLevel::Warn;
    if (n == "error") return LogLevel::Error;
    if (n == "off" || n == "none" || n == "quiet") return LogLevel::Off;
    return LogLevel::Info;  // an unrecognized --verbose value should still talk
}

void logRecord(LogLevel level, std::string_view message) {
    if (level < g_level.load(std::memory_order_relaxed)) return;
    // One formatted buffer, one write: records from worker threads interleave as
    // whole lines rather than tearing mid-message.
    const std::string line = fmt::format("[met-viewer] {}: {}\n", levelTag(level), message);
    std::fwrite(line.data(), 1, line.size(), stderr);
    std::fflush(stderr);
}

}  // namespace met::core
