#pragma once

#include <memory>

#include <QImage>
#include <QWidget>

#include "viewer/analysis/wind.h"
#include "viewer/core/field.h"
#include "viewer/render/colormap.h"

namespace met::app {

// A plain 2D scalar plot: the field drawn north-up in lat/lon axes. Emits a
// probe signal as the cursor moves so the status bar can show the value under
// the pointer.
class PlotView2D : public QWidget {
    Q_OBJECT
public:
    explicit PlotView2D(QWidget* parent = nullptr);

    // Show a field. The colormap range auto-fits the field's finite min/max.
    void setField(std::shared_ptr<core::Field2D> field);
    void setColormapByName(const QString& name);
    void clearField();

    // Contour overlay: when enabled, isolines are drawn at the given interval
    // (0 => auto). Passing enabled=false hides them.
    void setContoursEnabled(bool enabled);
    void setContourInterval(double interval);  // 0 = auto

    // Wind barbs overlay (mode 1 = barbs; 0/2 hide barbs in this view).
    void setWind(std::shared_ptr<analysis::WindField> wind);
    void setWindMode(int mode);

    [[nodiscard]] const render::Colormap& colormap() const { return cmap_; }
    [[nodiscard]] bool hasField() const { return field_ != nullptr; }

signals:
    // value is NaN and hasValue=false when the cursor is off-grid.
    void probeMoved(double lat, double lon, double value, bool hasValue);
    void probeLeft();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    QRectF plotRect() const;         // drawing area inside axis margins
    void rebuildImage();             // regenerate cached raster from field+cmap
    void autorange();                // fit cmap range to field

    // Map a fractional grid index (col, row) to a screen point in the plot rect.
    QPointF indexToScreen(double col, double row, const QRectF& r) const;

    std::shared_ptr<core::Field2D> field_;
    render::Colormap cmap_ = render::Colormap::builtin("viridis");
    QImage image_;                   // cached north-up raster
    core::BBox bbox_{};              // geographic extent of field_
    bool contoursEnabled_ = false;
    double contourInterval_ = 0.0;   // 0 = auto
    std::shared_ptr<analysis::WindField> wind_;
    int windMode_ = 0;
};

}  // namespace met::app
