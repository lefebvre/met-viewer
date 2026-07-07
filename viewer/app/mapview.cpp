#include "viewer/app/mapview.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <thread>
#include <variant>
#include <vector>

#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QPolygonF>
#include <QWheelEvent>

#include "viewer/analysis/sample.h"
#include "viewer/analysis/wind.h"
#include "viewer/app/tilelayer.h"
#include "viewer/render/tilemath.h"
#include "viewer/render/warp.h"
#include "viewer/render/windbarb.h"

namespace met::app {
namespace {
using render::tile::latToWorldY;
using render::tile::lonToWorldX;
using render::tile::worldXToLon;
using render::tile::worldYToLat;
constexpr int kTile = render::tile::kTileSize;
}  // namespace

MapView::MapView(TileLayer* tiles, QWidget* parent) : QOpenGLWidget(parent), tiles_(tiles) {
    setMouseTracking(true);
    setMinimumSize(320, 240);
    if (tiles_) connect(tiles_, &TileLayer::tileReady, this, &MapView::onTileReady);
}

void MapView::setGpuEnabled(bool on) {
    gpuEnabled_ = on;
    update();
}

void MapView::initializeGL() {
    initializeOpenGLFunctions();
    glReady_ = glField_.init();
}

double MapView::worldCenterX() const { return lonToWorldX(centerLon_, zoom_); }
double MapView::worldCenterY() const { return latToWorldY(centerLat_, zoom_); }
double MapView::topLeftWorldX() const { return worldCenterX() - width() / 2.0; }
double MapView::topLeftWorldY() const { return worldCenterY() - height() / 2.0; }

core::LatLon MapView::screenToLonLat(QPointF pos) const {
    return render::tile::worldToLonLat(topLeftWorldX() + pos.x(), topLeftWorldY() + pos.y(), zoom_);
}

QPointF MapView::lonLatToScreen(core::LatLon ll) const {
    return {lonToWorldX(ll.lon, zoom_) - topLeftWorldX(),
            latToWorldY(ll.lat, zoom_) - topLeftWorldY()};
}

void MapView::setField(std::shared_ptr<core::Field2D> field) {
    const bool firstField = (field_ == nullptr);
    field_ = std::move(field);
    if (field_) {
        if (autoRange_) autorange();
        if (firstField) fitToField();
    }
    update();
}

void MapView::setAutoRange(bool on) {
    autoRange_ = on;
    if (on && field_) {
        autorange();
        update();
    }
}

void MapView::setRange(double lo, double hi) {
    cmap_.setRange(lo, hi);
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
void MapView::setWind(std::shared_ptr<analysis::WindField> wind) {
    wind_ = std::move(wind);
    update();
}
void MapView::setWindMode(int mode) {
    windMode_ = mode;
    update();
}
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

bool MapView::drawFieldGpu() {
    if (!field_ || !glReady_) return false;
    const auto& g = std::get<core::RegularLatLonGrid>(field_->grid);

    // Re-upload the colormapped RGBA8 texture (grid order) when the field, the
    // colormap, or the range changes. The colormap is applied on the CPU over
    // the small grid; the GPU does the per-pixel warp.
    const QString cname = QString::fromStdString(cmap_.name());
    const bool dirty = uploadedField_ != field_.get() || uploadedCmap_ != cname ||
                       uploadedMin_ != cmap_.min() || uploadedMax_ != cmap_.max();
    if (dirty) {
        const std::size_t n = static_cast<std::size_t>(g.nlon) * static_cast<std::size_t>(g.nlat);
        std::vector<unsigned char> rgba(n * 4);
        for (std::size_t k = 0; k < n; ++k) {
            const render::Rgba c = cmap_.map(field_->values[k]);
            rgba[k * 4 + 0] = c.r;
            rgba[k * 4 + 1] = c.g;
            rgba[k * 4 + 2] = c.b;
            rgba[k * 4 + 3] = c.a;
        }
        glField_.uploadField(g.nlon, g.nlat, rgba.data());
        uploadedField_ = field_.get();
        uploadedCmap_ = cname;
        uploadedMin_ = cmap_.min();
        uploadedMax_ = cmap_.max();
    }

    const GlFieldRenderer::Grid grid{static_cast<float>(g.lon0), static_cast<float>(g.lat0),
                                     static_cast<float>(g.dlon), static_cast<float>(g.dlat),
                                     g.nlon, g.nlat, g.globalWrapLon};
    const GlFieldRenderer::View view{topLeftWorldX(), topLeftWorldY(),
                                     render::tile::worldSize(zoom_), width(), height()};
    glField_.render(grid, view, static_cast<float>(opacity_));
    return true;
}

void MapView::onTileReady(int, int, int) { update(); }

void MapView::paintGL() {
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

    // Warped field: GPU shader for regular lat/lon grids, else CPU warp.
    if (field_) {
        bool drewGpu = false;
        if (gpuEnabled_ && glReady_ &&
            std::holds_alternative<core::RegularLatLonGrid>(field_->grid)) {
            p.beginNativePainting();
            drewGpu = drawFieldGpu();
            p.endNativePainting();
        }
        if (!drewGpu) {
            ensureWarp();
            if (!warp_.isNull()) p.drawImage(0, 0, warp_);
        }
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

    // Wind overlay.
    if (wind_ && windMode_ == 1) drawBarbs(p);
    else if (wind_ && windMode_ == 2) drawStreamlines(p);

    // In-progress cross-section path.
    if (mode_ == Mode::CrossSection && !pathVertices_.empty()) {
        p.setRenderHint(QPainter::Antialiasing, true);
        QPolygonF poly;
        for (const core::LatLon& v : pathVertices_) poly << lonLatToScreen(v);
        p.setPen(QPen(QColor(220, 40, 40), 2.0));
        p.drawPolyline(poly);
        p.setBrush(QColor(220, 40, 40));
        for (const QPointF& pt : poly) p.drawEllipse(pt, 3.0, 3.0);
        p.setBrush(Qt::NoBrush);
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

void MapView::drawBarbs(QPainter& p) {
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(QColor(20, 20, 20, 220));
    pen.setWidthF(1.0);
    p.setPen(pen);
    p.setBrush(QColor(20, 20, 20, 220));

    const int spacing = 46;  // screen-space barb lattice
    for (int sy = spacing / 2; sy < height(); sy += spacing) {
        for (int sx = spacing / 2; sx < width(); sx += spacing) {
            const core::LatLon ll = screenToLonLat({double(sx), double(sy)});
            const analysis::UV uv = analysis::sampleWindLatLon(*wind_, ll);
            if (std::isnan(uv.u) || std::isnan(uv.v)) continue;
            const double speed = std::hypot(uv.u, uv.v);
            // Wind blows toward (u east, v north); in screen space north is -y.
            // The barb staff points in the direction the wind comes FROM.
            QPointF fromDir(-uv.u, uv.v);
            const render::WindBarb barb =
                render::makeWindBarb({double(sx), double(sy)}, fromDir,
                                     analysis::toKnots(speed), 22.0);
            if (barb.calm) {
                p.setBrush(Qt::NoBrush);
                p.drawEllipse(QPointF(sx, sy), 2.0, 2.0);
                p.setBrush(QColor(20, 20, 20, 220));
                continue;
            }
            for (const QLineF& l : barb.lines) p.drawLine(l);
            for (const QPolygonF& tri : barb.pennants) p.drawPolygon(tri);
        }
    }
}

void MapView::drawStreamlines(QPainter& p) {
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(QColor(25, 25, 35, 205));
    pen.setWidthF(1.2);
    p.setPen(pen);

    // Evenly-spaced streamlines (simplified Jobard-Lefer): seed on a coarse
    // lattice, integrate with RK4, and enforce separation via an occupancy grid.
    // A streamline is exempt from the cells it marked itself, so it can traverse
    // its own starting cell; it stops only when it meets a *different* line.
    const int cell = 16;
    const int gw = width() / cell + 2;
    const int gh = height() / cell + 2;
    std::vector<int> owner(static_cast<std::size_t>(gw) * static_cast<std::size_t>(gh), 0);
    int lineId = 0;

    auto cellOf = [&](QPointF s, std::size_t& idx) -> bool {
        const int gx = static_cast<int>(s.x()) / cell;
        const int gy = static_cast<int>(s.y()) / cell;
        if (gx < 0 || gy < 0 || gx >= gw || gy >= gh) return false;
        idx = static_cast<std::size_t>(gy) * static_cast<std::size_t>(gw) +
              static_cast<std::size_t>(gx);
        return true;
    };

    // Wind velocity as a unit vector in screen space (px), or false if invalid.
    auto velScreen = [&](QPointF s, double& vx, double& vy) -> bool {
        const analysis::UV uv = analysis::sampleWindLatLon(*wind_, screenToLonLat(s));
        if (std::isnan(uv.u) || std::isnan(uv.v)) return false;
        const double sp = std::hypot(uv.u, uv.v);
        if (sp < 1e-3) return false;
        vx = uv.u / sp;   // east -> +x
        vy = -uv.v / sp;  // north -> -y
        return true;
    };

    const double stepPx = 3.0;
    const int maxSteps = 300;
    for (int seedY = cell; seedY < height(); seedY += cell * 2) {
        for (int seedX = cell; seedX < width(); seedX += cell * 2) {
            std::size_t seedIdx = 0;
            if (!cellOf(QPointF(seedX, seedY), seedIdx) || owner[seedIdx] != 0) continue;
            ++lineId;
            for (int dir = -1; dir <= 1; dir += 2) {
                QPointF cur(seedX, seedY);
                QPolygonF line;
                line << cur;
                for (int step = 0; step < maxSteps; ++step) {
                    double k1x, k1y, k2x, k2y, k3x, k3y, k4x, k4y;
                    if (!velScreen(cur, k1x, k1y)) break;
                    if (!velScreen(cur + QPointF(0.5 * stepPx * dir * k1x, 0.5 * stepPx * dir * k1y),
                                   k2x, k2y)) break;
                    if (!velScreen(cur + QPointF(0.5 * stepPx * dir * k2x, 0.5 * stepPx * dir * k2y),
                                   k3x, k3y)) break;
                    if (!velScreen(cur + QPointF(stepPx * dir * k3x, stepPx * dir * k3y), k4x, k4y))
                        break;
                    cur += QPointF(stepPx * dir * (k1x + 2 * k2x + 2 * k3x + k4x) / 6.0,
                                   stepPx * dir * (k1y + 2 * k2y + 2 * k3y + k4y) / 6.0);
                    if (cur.x() < 0 || cur.y() < 0 || cur.x() >= width() || cur.y() >= height())
                        break;
                    std::size_t idx = 0;
                    if (!cellOf(cur, idx)) break;
                    if (owner[idx] != 0 && owner[idx] != lineId) break;  // meets another line
                    owner[idx] = lineId;
                    line << cur;
                }
                if (line.size() > 2) p.drawPolyline(line);
            }
        }
    }
}

void MapView::setInteractionMode(Mode mode) {
    mode_ = mode;
    if (mode_ != Mode::CrossSection) pathVertices_.clear();
    setCursor(mode_ == Mode::Pan ? Qt::ArrowCursor : Qt::CrossCursor);
    update();
}

void MapView::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) return;

    if (mode_ == Mode::Pan) {
        dragging_ = true;
        lastPos_ = event->position();
        return;
    }
    const core::LatLon ll = screenToLonLat(event->position());
    if (mode_ == Mode::Sounding) {
        emit soundingRequested(ll);
    } else if (mode_ == Mode::TimeSeries) {
        emit timeSeriesRequested(ll);
    } else if (mode_ == Mode::CrossSection) {
        pathVertices_.push_back(ll);
        update();
    }
}

void MapView::mouseDoubleClickEvent(QMouseEvent* event) {
    if (mode_ == Mode::CrossSection && event->button() == Qt::LeftButton) {
        if (pathVertices_.size() >= 2) emit crossSectionRequested(pathVertices_);
        pathVertices_.clear();
        update();
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
