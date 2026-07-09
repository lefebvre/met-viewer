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

// A dark palette in the spirit of the classic Fusion dark theme.
QPalette darkPalette() {
    const QColor window(0x2b, 0x2b, 0x2e);
    const QColor base(0x1e, 0x1e, 0x21);
    const QColor text(0xe6, 0xe6, 0xe6);
    const QColor disabled(0x7f, 0x7f, 0x7f);
    const QColor highlight(0x3d, 0x7e, 0xff);

    QPalette p;
    p.setColor(QPalette::Window, window);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, base);
    p.setColor(QPalette::AlternateBase, window);
    p.setColor(QPalette::ToolTipBase, window);
    p.setColor(QPalette::ToolTipText, text);
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::Button, window);
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::BrightText, Qt::red);
    p.setColor(QPalette::Link, highlight);
    p.setColor(QPalette::Highlight, highlight);
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::PlaceholderText, disabled);
    for (QPalette::ColorRole role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText}) {
        p.setColor(QPalette::Disabled, role, disabled);
    }
    p.setColor(QPalette::Disabled, QPalette::Highlight, QColor(0x3f, 0x3f, 0x44));
    return p;
}

bool systemIsDark() {
    return QApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
}

}  // namespace

ThemeManager::ThemeManager(QObject* parent) : QObject(parent) {
    mode_ = modeFromString(QSettings().value(kSettingsKey).toString());
    // Re-apply when the OS switches light/dark while we are following it.
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

void ThemeManager::apply() const {
    // Fusion honors QPalette on every platform, unlike some native styles; this
    // Qt build ships no native platform theme, so Fusion is the right base.
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    if (isDark()) {
        QApplication::setPalette(darkPalette());
    } else {
        QApplication::setPalette(QApplication::style()->standardPalette());
    }
}

}  // namespace met::app
