#pragma once

#include <QWidget>

class QFormLayout;
class QToolButton;
class QVBoxLayout;

namespace met::app {

// A titled, collapsible container for a view's controls: a header bar with a
// ▾/▸ toggle over a QFormLayout body. Collapsing hides the body, leaving just the
// title bar, so a view's controls can be minimized without losing the view.
class ControlPanel : public QWidget {
    Q_OBJECT
public:
    explicit ControlPanel(const QString& title, QWidget* parent = nullptr);

    void addRow(const QString& label, QWidget* w);  // labeled form row
    void addRow(QWidget* label, QWidget* w);         // labeled form row, icon/widget label
    void addRow(QWidget* w);                         // full-width row, no label
    void addBlock(QWidget* w);                        // full-width widget below the form (e.g. a colorbar)

    void setCollapsed(bool collapsed);
    [[nodiscard]] bool isCollapsed() const;

private:
    void toggleCollapsed();

    QToolButton* header_ = nullptr;
    QWidget* body_ = nullptr;
    QFormLayout* form_ = nullptr;
    QVBoxLayout* bodyLayout_ = nullptr;
};

// Pairs a canvas widget with its ControlPanel as [canvas | panel] in a splitter,
// so controls live with their view. The panel scrolls if it is taller than the
// frame. Used as the widget placed in a center tab.
class ViewFrame : public QWidget {
    Q_OBJECT
public:
    ViewFrame(QWidget* canvas, ControlPanel* panel, QWidget* parent = nullptr);

    [[nodiscard]] ControlPanel* panel() const { return panel_; }
    [[nodiscard]] QWidget* canvas() const { return canvas_; }

private:
    QWidget* canvas_ = nullptr;
    ControlPanel* panel_ = nullptr;
};

}  // namespace met::app
