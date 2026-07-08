#pragma once

#include <memory>
#include <vector>

#include <QImage>
#include <QOpenGLFunctions>
#include <QOpenGLWidget>
#include <QPointF>

#include "viewer/analysis/wind.h"
#include "viewer/app/coastlines.h"
#include "viewer/app/glfieldrenderer.h"
#include "viewer/core/field.h"
#include "viewer/render/colormap.h"

namespace met::app {

class TileLayer;

// A GIS map view: XYZ basemap tiles with the field warped into Web Mercator and
// composited on top at an adjustable opacity, plus a graticule and optional
// coastline overlay. Supports drag-pan and cursor-anchored wheel zoom.
//
// A regular lat/lon field is warped on the GPU (a fragment shader inverts the
// projection and colormaps in one pass); projected grids and GL-init failures
// fall back to the CPU warp.
class MapView : public QOpenGLWidget, protected QOpenGLFunctions {
    Q_OBJECT
public:
    explicit MapView(TileLayer* tiles, QWidget* parent = nullptr);
    ~MapView() override;

    void setGpuEnabled(bool on);

    void setField(std::shared_ptr<core::Field2D> field);
    void setColormapByName(const QString& name);
    void setAutoRange(bool on);
    void setRange(double lo, double hi);
    void setOpacity(double opacity);          // 0..1
    void setGraticuleVisible(bool on);
    void setCoastlinesVisible(bool on);
    void setCoastlines(std::shared_ptr<std::vector<GeoPolyline>> lines);
    void refreshSource();                     // basemap source changed

    // Wind overlay. Modes: 0 = off, 1 = barbs, 2 = streamlines.
    void setWind(std::shared_ptr<analysis::WindField> wind);
    void setWindMode(int mode);

    // Interaction mode: 0 = pan, 1 = cross-section path, 2 = sounding pick,
    // 3 = time-series pick.
    enum class Mode { Pan, CrossSection, Sounding, TimeSeries };
    void setInteractionMode(Mode mode);

    [[nodiscard]] const render::Colormap& colormap() const { return cmap_; }
    [[nodiscard]] bool hasField() const { return field_ != nullptr; }
    [[nodiscard]] QString units() const {
        return field_ ? QString::fromStdString(field_->meta.units) : QString();
    }

signals:
    void probeMoved(double lat, double lon, double value, bool hasValue);
    void probeLeft();
    // Emitted whenever the colormap value range changes so a per-view legend /
    // range spinners can follow.
    void rangeChanged(double lo, double hi);
    void crossSectionRequested(const std::vector<core::LatLon>& path);
    void soundingRequested(core::LatLon point);
    void timeSeriesRequested(core::LatLon point);

public slots:
    void onTileReady(int z, int x, int y);

protected:
    void initializeGL() override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;  // right-click: Fit to data

private:
    double worldCenterX() const;  // world-pixel coords of the view center
    double worldCenterY() const;
    double topLeftWorldX() const;
    double topLeftWorldY() const;
    core::LatLon screenToLonLat(QPointF pos) const;
    QPointF lonLatToScreen(core::LatLon ll) const;
    void fitToField();
    void ensureWarp();
    void autorange();
    void drawBarbs(QPainter& p);
    void drawStreamlines(QPainter& p);
    bool drawFieldGpu();  // returns true if the GPU path rendered the field

    TileLayer* tiles_ = nullptr;
    std::shared_ptr<core::Field2D> field_;
    render::Colormap cmap_ = render::Colormap::builtin("viridis");
    double opacity_ = 0.75;
    bool autoRange_ = true;
    bool gpuEnabled_ = false;  // opt-in; CPU warp is the robust default
    GlFieldRenderer glField_;
    bool glReady_ = false;
    const void* uploadedField_ = nullptr;  // field last pushed to the GPU
    QString uploadedCmap_;
    double uploadedMin_ = 0, uploadedMax_ = 0;
    bool graticule_ = true;
    bool coastlinesVisible_ = true;
    std::shared_ptr<std::vector<GeoPolyline>> coastlines_;
    std::shared_ptr<analysis::WindField> wind_;
    int windMode_ = 0;  // 0 off, 1 barbs, 2 streamlines

    int zoom_ = 2;
    double centerLon_ = 0.0;
    double centerLat_ = 20.0;

    // Warp cache: the last viewport we rendered for.
    QImage warp_;
    double warpTlx_ = 0, warpTly_ = 0;
    int warpZoom_ = -1, warpW_ = -1, warpH_ = -1;
    double warpOpacity_ = -1;
    const void* warpField_ = nullptr;
    QString warpCmap_;
    double warpMin_ = 0, warpMax_ = 0;

    bool dragging_ = false;
    QPointF lastPos_;

    Mode mode_ = Mode::Pan;
    std::vector<core::LatLon> pathVertices_;  // in-progress cross-section path
};

}  // namespace met::app
