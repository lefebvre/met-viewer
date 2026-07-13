#include "viewer/app/theme.h"

#include <QApplication>
#include <QColor>
#include <QPalette>
#include <QSettings>
#include <QString>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleHints>

namespace met::app {
namespace {

constexpr auto kSettingsKey = "theme/mode";

QString modeToString(ThemeManager::Mode m) {
    switch (m) {
        case ThemeManager::Mode::Light: return QStringLiteral("light");
        case ThemeManager::Mode::Dark: return QStringLiteral("dark");
        case ThemeManager::Mode::System: break;
    }
    return QStringLiteral("system");
}

ThemeManager::Mode modeFromString(const QString& s) {
    if (s == QLatin1String("light")) return ThemeManager::Mode::Light;
    if (s == QLatin1String("dark")) return ThemeManager::Mode::Dark;
    return ThemeManager::Mode::System;
}

bool systemIsDark() {
    return QApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
}

}  // namespace

ThemeManager::ThemeManager(QObject* parent) : QObject(parent) {
    mode_ = modeFromString(QSettings().value(kSettingsKey).toString());
    lastDark_ = isDark();  // seed so the first apply() doesn't emit spuriously
    // Re-apply when the OS switches light/dark while we are following it; apply()
    // emits effectiveSchemeChanged if the effective scheme actually changed.
    connect(QApplication::styleHints(), &QStyleHints::colorSchemeChanged, this,
            [this](Qt::ColorScheme) {
                if (mode_ == Mode::System) apply();
            });
    apply();
}

bool ThemeManager::isDark() const {
    return mode_ == Mode::Dark || (mode_ == Mode::System && systemIsDark());
}

void ThemeManager::setMode(Mode mode) {
    if (mode == mode_) return;
    mode_ = mode;
    QSettings().setValue(kSettingsKey, modeToString(mode));
    apply();
    emit modeChanged(mode);
}

void ThemeManager::apply() {
    // Fusion is a consistent base across platforms (this Qt build ships no native
    // platform theme). On Qt 6.8+ the Fusion palette is derived from the style
    // hint's color scheme, so a manual setPalette() gets overridden on polish;
    // drive the scheme directly instead. System -> Unknown means "follow the OS".
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QStyleHints* hints = QApplication::styleHints();
    switch (mode_) {
        case Mode::Dark: hints->setColorScheme(Qt::ColorScheme::Dark); break;
        case Mode::Light: hints->setColorScheme(Qt::ColorScheme::Light); break;
        case Mode::System: hints->setColorScheme(Qt::ColorScheme::Unknown); break;
    }
    const bool dark = isDark();
    if (dark != lastDark_) {
        lastDark_ = dark;
        emit effectiveSchemeChanged(dark);
    }
}

}  // namespace met::app
