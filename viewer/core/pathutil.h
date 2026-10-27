#pragma once

#include <cstdio>
#include <filesystem>
#include <string>

namespace met::core {

// A path as UTF-8 text, for messages and for C libraries that take UTF-8.
//
// Not path::string(): on Windows that converts to the ANSI code page, which
// mangles anything outside it and throws std::system_error for a character it
// cannot represent at all -- so an error message about a file named in Japanese
// became an exception of the wrong type instead of a message.
[[nodiscard]] inline std::string pathToUtf8(const std::filesystem::path& p) {
    const std::u8string s = p.u8string();
    return std::string(s.begin(), s.end());
}

// The inverse: UTF-8 text (what Qt's toStdString() and most C libraries produce)
// as a path. A narrow std::string handed straight to path's constructor is read
// in the ANSI code page on Windows, not as UTF-8.
[[nodiscard]] inline std::filesystem::path pathFromUtf8(const std::string& s) {
    return std::filesystem::path(std::u8string(s.begin(), s.end()));
}

// A path for a C library that takes a narrow path in the *system code page* on
// Windows -- netcdf-c, whose dpathmgr.c converts what it is given from CP_ACP
// before opening, so UTF-8 bytes there name a different file. The path in the
// code page when every character fits; otherwise its 8.3 short form, which is
// ASCII, when the volume keeps one; otherwise UTF-8, which then fails to open
// with an honest "cannot open" rather than opening something else. Elsewhere the
// native narrow path unchanged.
[[nodiscard]] std::string pathForCodePageApi(const std::filesystem::path& p);

// fopen for a path, through the wide API on Windows so any file name opens.
[[nodiscard]] inline std::FILE* openForReading(const std::filesystem::path& p) {
#ifdef _WIN32
    std::FILE* f = nullptr;
    return _wfopen_s(&f, p.c_str(), L"rb") == 0 ? f : nullptr;
#else
    return std::fopen(p.c_str(), "rb");
#endif
}

}  // namespace met::core
