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
    // Label each standard isobar with the altitude this sounding puts it at.
    // No-op for a sounding with no height data.
    void setHeightLabelsEnabled(bool on);
    [[nodiscard]] bool heightLabelsEnabled() const { return showHeights_; }
    // Whether the sounding carries geopotential height at all, i.e. whether the
    // labels toggle has anything to show.
    [[nodiscard]] bool hasHeights() const;
    // Decimals for the point's lat/lon in the title, from the source grid spacing
    // (see app::coordPrecision). The sounding carries no grid, so MainWindow sets it.
    void setCoordPrecision(int digits) { coordPrec_ = digits; }

    // The cursor readout currently on screen, one string per badge line; empty when
    // no readout is showing. Lets callers (and tests) read what the user is seeing.
    [[nodiscard]] QStringList hoverText() const {
        return hoverActive_ ? hoverLines_ : QStringList();
    }

    [[nodiscard]] QSize sizeHint() const override { return {480, 560}; }

signals:
    // Emitted on every setSounding with whether that sounding carries geopotential
    // height. The control panel is built before the first sounding arrives, so the
    // height-labels toggle learns whether it has anything to show from here.
    void heightsAvailableChanged(bool available);

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

        [[nodiscard]] double yOfP(double press) const;         // pressure (hPa) -> y
        [[nodiscard]] double pOfY(double y) const;             // y -> pressure (hPa)
        [[nodiscard]] double xOfT(double tC, double y) const;  // temp (°C) at row y -> x
        [[nodiscard]] double tOfX(double x, double y) const;   // x at row y -> temp (°C)
    };
    [[nodiscard]] Layout layout() const;

    analysis::Sounding s_;
    int coordPrec_ = 2;  // lat/lon decimals in the title
    bool showHeights_ = true;

    // Cursor readout state; cleared when the cursor leaves.
    bool hoverActive_ = false;
    QPointF hoverPos_;
    QStringList hoverLines_;
};

}  // namespace met::app
