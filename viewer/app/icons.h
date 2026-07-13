#pragma once

#include <QIcon>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVector>

class QAbstractButton;
class QAction;
class QComboBox;
class QLabel;
class QWidget;

namespace met::app {

class ThemeManager;

// Builds theme-aware QIcons from the embedded glyph palette
// (`:/icons/{dark,light}/<token>_NN.png`) and keeps the widgets it was asked to
// icon-ify in sync when the effective light/dark scheme changes.
//
// The glyph set follows the README's inverted convention: the `dark/` glyphs are
// near-black (for a LIGHT ui) and `light/` glyphs are off-white (for a DARK ui),
// so `iconFor` picks `light/` when the theme is dark and vice versa.
class IconThemer : public QObject {
    Q_OBJECT
public:
    explicit IconThemer(ThemeManager* theme, QObject* parent = nullptr);

    [[nodiscard]] ThemeManager* theme() const { return theme_; }

    // A multi-size icon for `token` in the currently active variant.
    [[nodiscard]] QIcon iconFor(const QString& token) const;

    // Set the icon now and re-apply it on every future theme change. Each helper
    // preserves the widget's accessible text (tooltip / accessibleName) so
    // icon-only controls stay identifiable to menus, accessibility, and tests.
    void applyAction(QAction* action, const QString& token);
    void applyButton(QAbstractButton* button, const QString& token);
    void applyComboItem(QComboBox* combo, int index, const QString& token);
    // Sets the widget's windowIcon (e.g. a QDockWidget, shown on its tab).
    void applyWindowIcon(QWidget* widget, const QString& token);

    // A QLabel showing `token` at `px`, usable as an icon-only form-row label.
    QLabel* iconLabel(const QString& token, int px, const QString& tooltip,
                      QWidget* parent = nullptr);

private:
    void refresh();

    ThemeManager* theme_;

    struct ActionReg { QPointer<QAction> w; QString token; };
    struct ButtonReg { QPointer<QAbstractButton> w; QString token; };
    struct ComboReg { QPointer<QComboBox> w; int index; QString token; };
    struct LabelReg { QPointer<QLabel> w; QString token; int px; };
    struct WindowIconReg { QPointer<QWidget> w; QString token; };

    QVector<ActionReg> actions_;
    QVector<ButtonReg> buttons_;
    QVector<ComboReg> comboItems_;
    QVector<LabelReg> labels_;
    QVector<WindowIconReg> windowIcons_;
};

}  // namespace met::app
