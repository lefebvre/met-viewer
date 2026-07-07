#pragma once

#include <memory>

#include <QImage>
#include <QWidget>

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

    std::shared_ptr<core::Field2D> field_;
    render::Colormap cmap_ = render::Colormap::builtin("viridis");
    QImage image_;                   // cached north-up raster
    core::BBox bbox_{};              // geographic extent of field_
};

}  // namespace met::app
