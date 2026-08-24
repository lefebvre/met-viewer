#pragma once

#include <cstddef>

namespace met::app {

// The user-tunable settings that are not view state.
//
// These were three loose ints on MainWindow (`cacheBudgetMB_`, `animationFps_`,
// `prefetchAhead_`) read and written in four places each — the preferences dialog,
// loadSettings, saveSettings, and the code that actually applies them. Grouping
// them means the dialog and the settings round-trip move one object, and the
// defaults live in exactly one place instead of being repeated as literals at
// every QSettings::value call.
//
// Deliberately a plain struct with no Qt dependency: the values are data, and the
// QSettings round-trip belongs to whoever owns the settings file.
struct Preferences {
    // Field cache budget. The default is 1 GB — large enough to hold a full day of
    // HRRR slabs for smooth playback, small enough not to surprise anyone.
    int cacheBudgetMB = 1024;

    // Animation frame rate. Playback is closed-loop (a frame advances only once the
    // decode and every open analysis extraction have settled), so this is an upper
    // bound rather than a guarantee.
    int animationFps = 6;

    // How many upcoming time steps to decode ahead during playback.
    int prefetchAhead = 4;

    [[nodiscard]] std::size_t cacheBudgetBytes() const {
        return static_cast<std::size_t>(cacheBudgetMB) * 1024 * 1024;
    }

    // Ranges the preferences dialog enforces, kept next to the defaults so the
    // dialog cannot drift from what the values mean.
    static constexpr int kMinCacheMB = 16;
    static constexpr int kMaxCacheMB = 16384;
    static constexpr int kMinFps = 1;
    static constexpr int kMaxFps = 30;
};

}  // namespace met::app
