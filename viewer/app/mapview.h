#pragma once

#include <memory>
#include <vector>

#include <QImage>
#include <QPointF>
#include <QWidget>

#include "viewer/analysis/wind.h"
#include "viewer/app/coastlines.h"
#include "viewer/core/field.h"
#include "viewer/render/colormap.h"

namespace met::app {

class TileLayer;

// A GIS map view: XYZ basemap tiles with the field warped into Web Mercator and
// composited on top at an adjustable opacity, plus a graticule and optional
// coastline overlay. Supports drag-pan and cursor-anchored wheel zoom.
class MapView : public QWidget {
    Q_OBJECT
public:
    explicit MapView(TileLayer* tiles, QWidget* parent = nullptr);

    void setField(std::shared_ptr<core::Field2D> field);
    void setColormapByName(const QString& name);
    void setOpacity(double opacity);          // 0..1
    void setGraticuleVisible(bool on);
    void setCoastlinesVisible(bool on);
    void setCoastlines(std::shared_ptr<std::vector<GeoPolyline>> lines);
    void refreshSource();                     // basemap source changed

    // Wind overlay. Modes: 0 = off, 1 = barbs, 2 = streamlines.
    void setWind(std::shared_ptr<analysis::WindField> wind);
    void setWindMode(int mode);

    [[nodiscard]] const render::Colormap& colormap() const { return cmap_; }
    [[nodiscard]] bool hasField() const { return field_ != nullptr; }

signals:
    void probeMoved(double lat, double lon, double value, bool hasValue);
    void probeLeft();

public slots:
    void onTileReady(int z, int x, int y);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void leaveEvent(QEvent* event) override;

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

    TileLayer* tiles_ = nullptr;
    std::shared_ptr<core::Field2D> field_;
    render::Colormap cmap_ = render::Colormap::builtin("viridis");
    double opacity_ = 0.75;
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
};

}  // namespace met::app
