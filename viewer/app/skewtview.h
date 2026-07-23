#pragma once

#include <QPointF>
#include <QRectF>
#include <QStringList>
#include <QWidget>

#include "viewer/analysis/sounding.h"

namespace met::app {

// A skew-T log-p thermodynamic diagram: skewed isotherms, log-pressure isobars,
// dry adiabats, and saturation mixing-ratio lines in the background, with the
// sounding's temperature and dewpoint traces on top.
class SkewTView : public QWidget {
    Q_OBJECT
public:
    explicit SkewTView(QWidget* parent = nullptr);
    void setSounding(const analysis::Sounding& s);

    // The cursor readout currently on screen, one string per badge line; empty when
    // no readout is showing. Lets callers (and tests) read what the user is seeing.
    [[nodiscard]] QStringList hoverText() const {
        return hoverActive_ ? hoverLines_ : QStringList();
    }

    [[nodiscard]] QSize sizeHint() const override { return {480, 560}; }

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    // The diagram area and its skew-T log-p transform, in both directions. Shared
    // by paintEvent and the cursor readout so the traces and the hovered
    // pressure/temperature can never disagree about the mapping.
    struct Layout {
        QRectF rect;
        bool valid = false;

        [[nodiscard]] double yOfP(double press) const;   // pressure (hPa) -> y
        [[nodiscard]] double pOfY(double y) const;       // y -> pressure (hPa)
        [[nodiscard]] double xOfT(double tC, double y) const;  // temp (°C) at row y -> x
        [[nodiscard]] double tOfX(double x, double y) const;   // x at row y -> temp (°C)
    };
    [[nodiscard]] Layout layout() const;

    analysis::Sounding s_;

    // Cursor readout state; cleared when the cursor leaves.
    bool hoverActive_ = false;
    QPointF hoverPos_;
    QStringList hoverLines_;
};

}  // namespace met::app
