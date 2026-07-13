#pragma once

#include <QObject>

namespace met::app {

// Manages the application-wide light/dark theme. Applies the Fusion style with a
// light or dark QPalette so the appearance is consistent across platforms and
// honors palette() — the custom views (2D plot, skew-T, cross-section, time
// series) already paint from palette(), so they follow automatically.
//
// Mode::System tracks the OS color scheme live (via QStyleHints). The choice is
// persisted in QSettings under "theme/mode".
class ThemeManager : public QObject {
    Q_OBJECT
public:
    enum class Mode { System, Light, Dark };
    Q_ENUM(Mode)

    // Loads the saved mode (default System), applies it immediately, and starts
    // tracking system color-scheme changes.
    explicit ThemeManager(QObject* parent = nullptr);

    [[nodiscard]] Mode mode() const { return mode_; }

    // Sets the mode, applies it, and persists the choice. No-op if unchanged.
    void setMode(Mode mode);

    // The effective scheme after resolving System against the OS. True if dark.
    [[nodiscard]] bool isDark() const;

signals:
    void modeChanged(Mode mode);
    // Emitted whenever the *effective* light/dark scheme changes — from a user
    // mode change or a live OS scheme switch while in System mode. This is the
    // trigger for reloading theme-variant icons.
    void effectiveSchemeChanged(bool dark);

private:
    void apply();

    Mode mode_ = Mode::System;
    bool lastDark_ = false;
};

}  // namespace met::app
