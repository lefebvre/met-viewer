#include "viewer/app/icons.h"

#include <QAbstractButton>
#include <QAction>
#include <QComboBox>
#include <QLabel>
#include <QWidget>

#include "viewer/app/theme.h"

namespace met::app {

namespace {
constexpr int kGlyphSizes[] = {16, 24, 32, 48};
}  // namespace

IconThemer::IconThemer(ThemeManager* theme, QObject* parent)
    : QObject(parent), theme_(theme) {
    connect(theme_, &ThemeManager::effectiveSchemeChanged, this, &IconThemer::refresh);
}

QIcon IconThemer::iconFor(const QString& token) const {
    // Inverted convention: off-white (`light/`) glyphs on a dark UI, near-black
    // (`dark/`) glyphs on a light UI.
    const QString variant = theme_->isDark() ? QStringLiteral("light") : QStringLiteral("dark");
    QIcon icon;
    for (int px : kGlyphSizes) {
        icon.addFile(QStringLiteral(":/icons/%1/%2_%3.png").arg(variant, token).arg(px));
    }
    return icon;
}

void IconThemer::applyAction(QAction* action, const QString& token) {
    if (!action) return;
    action->setIcon(iconFor(token));
    if (action->toolTip().isEmpty()) action->setToolTip(action->text());
    actions_.push_back({action, token});
}

void IconThemer::applyButton(QAbstractButton* button, const QString& token) {
    if (!button) return;
    button->setIcon(iconFor(token));
    buttons_.push_back({button, token});
}

void IconThemer::applyComboItem(QComboBox* combo, int index, const QString& token) {
    if (!combo || index < 0 || index >= combo->count()) return;
    combo->setItemIcon(index, iconFor(token));
    comboItems_.push_back({combo, index, token});
}

void IconThemer::applyWindowIcon(QWidget* widget, const QString& token) {
    if (!widget) return;
    widget->setWindowIcon(iconFor(token));
    windowIcons_.push_back({widget, token});
}

QLabel* IconThemer::iconLabel(const QString& token, int px, const QString& tooltip,
                              QWidget* parent) {
    auto* label = new QLabel(parent);
    label->setPixmap(iconFor(token).pixmap(px));
    label->setToolTip(tooltip);
    label->setAccessibleName(tooltip);
    labels_.push_back({label, token, px});
    return label;
}

void IconThemer::refresh() {
    for (const auto& r : actions_)
        if (r.w) r.w->setIcon(iconFor(r.token));
    for (const auto& r : buttons_)
        if (r.w) r.w->setIcon(iconFor(r.token));
    for (const auto& r : comboItems_)
        if (r.w && r.index < r.w->count()) r.w->setItemIcon(r.index, iconFor(r.token));
    for (const auto& r : labels_)
        if (r.w) r.w->setPixmap(iconFor(r.token).pixmap(r.px));
    for (const auto& r : windowIcons_)
        if (r.w) r.w->setWindowIcon(iconFor(r.token));
}

}  // namespace met::app
