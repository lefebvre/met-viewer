#include "viewer/app/mapview.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <thread>

#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QPolygonF>
#include <QWheelEvent>

#include "viewer/analysis/sample.h"
#include "viewer/app/tilelayer.h"
#include "viewer/render/tilemath.h"
#include "viewer/render/warp.h"

namespace met::app {
namespace {
using render::tile::latToWorldY;
using render::tile::lonToWorldX;
using render::tile::worldXToLon;
using render::tile::worldYToLat;
constexpr int kTile = render::tile::kTileSize;
}  // namespace

MapView::MapView(TileLayer* tiles, QWidget* parent) : QWidget(parent), tiles_(tiles) {
    setMouseTracking(true);
    setMinimumSize(320, 240);
    if (tiles_) connect(tiles_, &TileLayer::tileReady, this, &MapView::onTileReady);
}

double MapView::worldCenterX() const { return lonToWorldX(centerLon_, zoom_); }
double MapView::worldCenterY() const { return latToWorldY(centerLat_, zoom_); }
double MapView::topLeftWorldX() const { return worldCenterX() - width() / 2.0; }
double MapView::topLeftWorldY() const { return worldCenterY() - height() / 2.0; }

core::LatLon MapView::screenToLonLat(QPointF pos) const {
    return render::tile::worldToLonLat(topLeftWorldX() + pos.x(), topLeftWorldY() + pos.y(), zoom_);
}

void MapView::setField(std::shared_ptr<core::Field2D> field) {
    const bool firstField = (field_ == nullptr);
    field_ = std::move(field);
    if (field_) {
        autorange();
        if (firstField) fitToField();
    }
    update();
}

void MapView::setColormapByName(const QString& name) {
    const double lo = cmap_.min(), hi = cmap_.max();
    cmap_ = render::Colormap::builtin(name.toStdString());
    cmap_.setRange(lo, hi);
    update();
}

void MapView::setOpacity(double opacity) {
    opacity_ = std::clamp(opacity, 0.0, 1.0);
    update();
}

void MapView::setGraticuleVisible(bool on) { graticule_ = on; update(); }
void MapView::setCoastlinesVisible(bool on) { coastlinesVisible_ = on; update(); }
void MapView::setCoastlines(std::shared_ptr<std::vector<GeoPolyline>> lines) {
    coastlines_ = std::move(lines);
    update();
}
void MapView::refreshSource() { update(); }

void MapView::autorange() {
    double lo = std::numeric_limits<double>::infinity();
    double hi = -std::numeric_limits<double>::infinity();
    for (float v : field_->values) {
        if (std::isnan(v)) continue;
        lo = std::min(lo, static_cast<double>(v));
        hi = std::max(hi, static_cast<double>(v));
    }
    if (!std::isfinite(lo) || !std::isfinite(hi) || lo == hi) { lo = 0; hi = 1; }
    cmap_.setRange(lo, hi);
}

void MapView::fitToField() {
    const core::BBox b = core::gridBBox(field_->grid);
    centerLon_ = 0.5 * (b.minLon + b.maxLon);
    centerLat_ = 0.5 * (b.minLat + b.maxLat);
    const int zx = render::tile::zoomForLonSpan(b.maxLon - b.minLon, std::max(64, width() - 8));
    // Constrain by the latitude span too (Mercator-projected height).
    const double latPxSpan =
        std::abs(latToWorldY(b.minLat, zx) - latToWorldY(b.maxLat, zx));
    int z = zx;
    while (z > 0 && latPxSpan * std::pow(2.0, z - zx) > std::max(64, height() - 8)) --z;
    zoom_ = std::clamp(z, 0, 19);
}

void MapView::ensureWarp() {
    if (!field_) { warp_ = {}; return; }
    const double tlx = topLeftWorldX(), tly = topLeftWorldY();
    const bool same = warpField_ == field_.get() && warpZoom_ == zoom_ && warpW_ == width() &&
                      warpH_ == height() && warpTlx_ == tlx && warpTly_ == tly &&
                      warpOpacity_ == opacity_ && warpCmap_ == QString::fromStdString(cmap_.name()) &&
                      warpMin_ == cmap_.min() && warpMax_ == cmap_.max();
    if (same && !warp_.isNull()) return;

    render::MercatorViewport view{tlx, tly, zoom_, width(), height()};
    const int hw = std::max(1, static_cast<int>(std::thread::hardware_concurrency()) - 1);
    warp_ = render::warpToMercator(*field_, cmap_, view, opacity_, hw);
    warpField_ = field_.get();
    warpZoom_ = zoom_; warpW_ = width(); warpH_ = height();
    warpTlx_ = tlx; warpTly_ = tly; warpOpacity_ = opacity_;
    warpCmap_ = QString::fromStdString(cmap_.name());
    warpMin_ = cmap_.min(); warpMax_ = cmap_.max();
}

void MapView::onTileReady(int, int, int) { update(); }

void MapView::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), QColor(210, 220, 230));  // neutral ocean while tiles load

    const double tlx = topLeftWorldX(), tly = topLeftWorldY();

    // Basemap tiles.
    if (tiles_) {
        const int n = 1 << zoom_;
        const int x0 = static_cast<int>(std::floor(tlx / kTile));
        const int y0 = static_cast<int>(std::floor(tly / kTile));
        const int x1 = static_cast<int>(std::floor((tlx + width()) / kTile));
        const int y1 = static_cast<int>(std::floor((tly + height()) / kTile));
        for (int ty = y0; ty <= y1; ++ty) {
            for (int tx = x0; tx <= x1; ++tx) {
                const int wx = ((tx % n) + n) % n;  // wrap longitude
                if (ty < 0 || ty >= n) continue;
                const QImage img = tiles_->tile(zoom_, wx, ty);
                const double sx = tx * kTile - tlx;
                const double sy = ty * kTile - tly;
                if (!img.isNull())
                    p.drawImage(QRectF(sx, sy, kTile, kTile), img);
                else {
                    p.setPen(QColor(200, 205, 210));
                    p.drawRect(QRectF(sx, sy, kTile, kTile));
                }
            }
        }
    }

    // Warped field.
    if (field_) {
        ensureWarp();
        if (!warp_.isNull()) p.drawImage(0, 0, warp_);
    }

    // Graticule.
    if (graticule_) {
        p.setRenderHint(QPainter::Antialiasing, true);
        QPen gpen(QColor(0, 0, 0, 60));
        gpen.setWidthF(0.6);
        p.setPen(gpen);
        const core::LatLon tl = screenToLonLat({0, 0});
        const core::LatLon br = screenToLonLat({double(width()), double(height())});
        auto niceStep = [](double span) {
            const double s[] = {1, 2, 5, 10, 15, 30, 45};
            for (double v : s) if (span / v <= 8) return v;
            return 60.0;
        };
        const double lonStep = niceStep(std::abs(br.lon - tl.lon));
        const double latStep = niceStep(std::abs(tl.lat - br.lat));
        for (double lon = std::ceil(tl.lon / lonStep) * lonStep; lon <= br.lon; lon += lonStep) {
            const double x = lonToWorldX(lon, zoom_) - tlx;
            p.drawLine(QPointF(x, 0), QPointF(x, height()));
            p.drawText(QPointF(x + 2, 12), QString::number(lon, 'g', 4) + "°");
        }
        for (double lat = std::floor(br.lat / latStep) * latStep; lat <= tl.lat; lat += latStep) {
            const double y = latToWorldY(lat, zoom_) - tly;
            p.drawLine(QPointF(0, y), QPointF(width(), y));
            p.drawText(QPointF(2, y - 2), QString::number(lat, 'g', 4) + "°");
        }
    }

    // Coastlines.
    if (coastlinesVisible_ && coastlines_) {
        p.setRenderHint(QPainter::Antialiasing, true);
        QPen cpen(QColor(40, 40, 40, 200));
        cpen.setWidthF(0.9);
        p.setPen(cpen);
        for (const auto& line : *coastlines_) {
            QPolygonF poly;
            poly.reserve(static_cast<int>(line.size()));
            for (const QPointF& ll : line) {
                const double x = lonToWorldX(ll.x(), zoom_) - tlx;
                const double y = latToWorldY(ll.y(), zoom_) - tly;
                poly << QPointF(x, y);
            }
            p.drawPolyline(poly);
        }
    }

    // Attribution.
    if (tiles_ && !tiles_->source().attribution.isEmpty()) {
        p.setRenderHint(QPainter::Antialiasing, true);
        const QString text = tiles_->source().attribution;
        QFontMetrics fm(p.font());
        const QRectF box = fm.boundingRect(text).adjusted(-4, -2, 4, 2);
        const QRectF at(width() - box.width() - 4, height() - box.height() - 4, box.width(),
                        box.height());
        p.fillRect(at, QColor(255, 255, 255, 180));
        p.setPen(QColor(20, 20, 20));
        p.drawText(at, Qt::AlignCenter, text);
    }

    if (!field_) {
        p.setPen(QColor(60, 60, 60));
        p.drawText(rect(), Qt::AlignCenter, tr("Open a file to drape a field over the map"));
    }
}

void MapView::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        dragging_ = true;
        lastPos_ = event->position();
    }
}

void MapView::mouseMoveEvent(QMouseEvent* event) {
    if (dragging_) {
        const QPointF delta = event->position() - lastPos_;
        lastPos_ = event->position();
        // Move the view opposite to the drag: recover center from shifted world.
        const double cx = worldCenterX() - delta.x();
        const double cy = worldCenterY() - delta.y();
        centerLon_ = worldXToLon(cx, zoom_);
        centerLat_ = std::clamp(worldYToLat(cy, zoom_), -render::tile::kMaxLat, render::tile::kMaxLat);
        update();
        return;
    }
    // Probe.
    if (!field_) { emit probeLeft(); return; }
    const core::LatLon ll = screenToLonLat(event->position());
    const float v = analysis::sampleBilinear(*field_, ll);
    emit probeMoved(ll.lat, ll.lon, static_cast<double>(v), !std::isnan(v));
}

void MapView::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) dragging_ = false;
}

void MapView::wheelEvent(QWheelEvent* event) {
    const int steps = event->angleDelta().y() > 0 ? 1 : -1;
    const int newZoom = std::clamp(zoom_ + steps, 0, 19);
    if (newZoom == zoom_) return;

    // Keep the lon/lat under the cursor fixed across the zoom change.
    const QPointF cursor = event->position();
    const core::LatLon anchor = screenToLonLat(cursor);
    zoom_ = newZoom;
    const double cx = lonToWorldX(anchor.lon, zoom_) - (cursor.x() - width() / 2.0);
    const double cy = latToWorldY(anchor.lat, zoom_) - (cursor.y() - height() / 2.0);
    centerLon_ = worldXToLon(cx, zoom_);
    centerLat_ = std::clamp(worldYToLat(cy, zoom_), -render::tile::kMaxLat, render::tile::kMaxLat);
    update();
}

void MapView::leaveEvent(QEvent*) { emit probeLeft(); }

}  // namespace met::app
