#pragma once

#include <memory>

#include <QImage>
#include <QPointF>
#include <QStringList>
#include <QWidget>

#include "viewer/analysis/wind.h"
#include "viewer/core/field.h"
#include "viewer/render/colormap.h"
#include "viewer/render/contour.h"

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

    void setAutoRange(bool on);         // re-fit the colormap to the field
    void setRange(double lo, double hi);  // manual colormap range

    // Wind barbs overlay (mode 1 = barbs; 0/2 hide barbs in this view).
    void setWind(std::shared_ptr<analysis::WindField> wind);
    void setWindMode(int mode);

    // The cursor readout currently on screen, one string per badge line; empty when
    // no readout is showing. Lets callers (and tests) read what the user is seeing.
    [[nodiscard]] QStringList hoverText() const {
        return hoverActive_ ? hoverLines_ : QStringList();
    }

    [[nodiscard]] const render::Colormap& colormap() const { return cmap_; }
    [[nodiscard]] bool hasField() const { return field_ != nullptr; }
    [[nodiscard]] QString units() const {
        return field_ ? QString::fromStdString(field_->meta.units) : QString();
    }

signals:
    // value is NaN and hasValue=false when the cursor is off-grid.
    void probeMoved(double lat, double lon, double value, bool hasValue);
    void probeLeft();
    // Emitted whenever the colormap value range changes (auto-fit or manual) so a
    // per-view legend / range spinners can follow.
    void rangeChanged(double lo, double hi);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    QRectF plotRect() const;         // drawing area inside axis margins
    void rebuildImage();             // regenerate cached raster from field+cmap
    void autorange();                // fit cmap range to field

    // The view draws the field in flat grid-index space (see indexToScreen), so
    // index<->screen is the primitive and geography is derived from it via the
    // grid. On a regular lat/lon grid index space *is* linear in lat/lon; on a
    // projected grid it is not, and treating it as if it were misplaces a probe by
    // hundreds of kilometres near the corners.
    QPointF indexToScreen(double col, double row, const QRectF& r) const;
    // Inverse of indexToScreen. Returns false when the point is outside the grid.
    bool screenToIndex(QPointF pos, const QRectF& r, double& col, double& row) const;
    // True when the field's grid is a regular lat/lon one, whose index space maps
    // linearly onto the lat/lon axes this view labels.
    [[nodiscard]] bool isLatLonGrid() const;

    // Lat/lon graticule traced through index space, for grids whose axes are not
    // lat/lon (projected/ARL) and so cannot carry geography in straight ticks.
    void drawGraticule(QPainter& p, const QRectF& r) const;

    // Badge lines for the cursor readout: position, value, and grid cell.
    QStringList hoverTextAt(core::LatLon ll, float value, double col, double row) const;

    std::shared_ptr<core::Field2D> field_;
    render::Colormap cmap_ = render::Colormap::builtin("viridis");
    QImage image_;                   // cached north-up raster
    core::BBox bbox_{};              // geographic extent of field_
    bool contoursEnabled_ = false;
    double contourInterval_ = 0.0;   // 0 = auto
    render::ContourCache contours_;  // isolines survive the per-mouse-move repaints
    bool autoRange_ = true;
    std::shared_ptr<analysis::WindField> wind_;
    int windMode_ = 0;

    // Cursor readout state: the badge follows the pointer, so these change on every
    // mouse-move and are cleared when the cursor leaves.
    bool hoverActive_ = false;
    QPointF hoverPos_;
    QStringList hoverLines_;
};

}  // namespace met::app
