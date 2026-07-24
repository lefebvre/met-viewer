#pragma once

#include <QObject>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QStringList>

class QPainter;
class QPalette;

namespace met::app {

// The views that can paint a cursor readout. Used to key HoverOptions.
enum class HoverView { Plot, Map, CrossSection, TimeSeries, SkewT };

// App-wide on/off state for the cursor readout, one flag per view type, persisted
// in QSettings under "hover/<view>" (default on). The analysis views are created
// on demand (one per dock), so pushing a setter into each construction site would
// also mean fishing every open view back out on each toggle; instead the views
// read this object and repaint on changed(), and docks opened later pick up the
// current setting for free.
class HoverOptions : public QObject {
    Q_OBJECT
public:
    static HoverOptions& instance();

    [[nodiscard]] bool enabled(HoverView v) const;
    void setEnabled(HoverView v, bool on);

signals:
    // Emitted for the one view type whose flag changed.
    void changed(HoverView v);

private:
    HoverOptions();

    bool enabled_[5];  // indexed by HoverView
};

// Draw a cursor readout: crosshair lines through `at` clipped to `plotRect`, plus a
// translucent text box of `lines` placed beside the cursor and nudged to stay inside
// `plotRect`. Colors come from `pal` so the badge follows the light/dark theme.
// No-op when `lines` is empty or `at` is outside `plotRect`.
void paintHoverReadout(QPainter& p, const QRectF& plotRect, QPointF at,
                       const QStringList& lines, const QPalette& pal);

// "273.15 K (0.00 °C)" — the native value plus the preferred display unit when one
// exists (see core::preferredDisplayUnit). Empty `units` yields just the number.
[[nodiscard]] QString formatValueWithUnits(double value, const QString& units);

// Decimal places for a lat/lon readout, chosen from the source grid spacing
// (core::gridSpacingDeg): 2 (~1 km) for synoptic-scale grids, 3 (~0.1 km) for
// high-resolution convection-allowing data finer than ~5 km. `spacingDeg <= 0`
// (unknown) yields 2.
[[nodiscard]] int coordPrecision(double spacingDeg);

}  // namespace met::app
