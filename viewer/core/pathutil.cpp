#include "viewer/core/pathutil.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace met::core {

#ifdef _WIN32
namespace {

// `w` in the system code page, or nothing when a character has no exact
// equivalent there (WideCharToMultiByte would otherwise substitute '?').
bool toCodePage(const std::wstring& w, std::string& out) {
    BOOL lossy = FALSE;
    const int n = WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, w.c_str(), -1, nullptr, 0,
                                      nullptr, &lossy);
    if (n <= 0 || lossy) return false;
    std::string s(static_cast<std::size_t>(n), '\0');
    if (WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, w.c_str(), -1, s.data(), n, nullptr,
                            &lossy) <= 0 ||
        lossy)
        return false;
    s.resize(static_cast<std::size_t>(n - 1));  // the terminator
    out = std::move(s);
    return true;
}

}  // namespace

std::string pathForCodePageApi(const std::filesystem::path& p) {
    const std::wstring wide = p.wstring();
    std::string narrow;
    if (toCodePage(wide, narrow)) return narrow;

    const DWORD len = GetShortPathNameW(wide.c_str(), nullptr, 0);
    if (len > 0) {
        std::wstring shortPath(len, L'\0');
        const DWORD written = GetShortPathNameW(wide.c_str(), shortPath.data(), len);
        if (written > 0 && written < len) {
            shortPath.resize(written);
            if (toCodePage(shortPath, narrow)) return narrow;
        }
    }
    return pathToUtf8(p);
}
#else
std::string pathForCodePageApi(const std::filesystem::path& p) { return p.native(); }
#endif

}  // namespace met::core
