#include "viewer/render/warp.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <thread>
#include <variant>
#include <vector>

#include "viewer/render/tilemath.h"

namespace met::render {
namespace {

using core::RegularLatLonGrid;

// NaN-aware bilinear sample at fractional (x, y). Inlined here to keep met_render
// independent of met_analysis. Returns NaN if all corners are missing.
float sampleBilinear(const core::Field2D& f, double x, double y) {
    const int w = f.width();
    const int h = f.height();
    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(y));
    const int x1 = std::min(x0 + 1, w - 1);
    const int y1 = std::min(y0 + 1, h - 1);
    const double fx = x - x0;
    const double fy = y - y0;

    const float v00 = f.at(x0, y0);
    const float v10 = f.at(x1, y0);
    const float v01 = f.at(x0, y1);
    const float v11 = f.at(x1, y1);

    double acc = 0.0, wsum = 0.0, bestW = -1.0;
    float best = std::numeric_limits<float>::quiet_NaN();
    auto add = [&](float v, double weight) {
        if (!std::isnan(v)) {
            acc += weight * v;
            wsum += weight;
            if (weight > bestW) { bestW = weight; best = v; }
        }
    };
    add(v00, (1 - fx) * (1 - fy));
    add(v10, fx * (1 - fy));
    add(v01, (1 - fx) * fy);
    add(v11, fx * fy);
    if (wsum <= 0.0) return std::numeric_limits<float>::quiet_NaN();
    return wsum > 0.999 ? static_cast<float>(acc / wsum) : best;
}

}  // namespace

QImage warpToMercator(const core::Field2D& field, const Colormap& cmap,
                      const MercatorViewport& view, double opacity, int threads) {
    QImage img(view.width, view.height, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    if (view.width <= 0 || view.height <= 0) return img;

    const auto& g = std::get<RegularLatLonGrid>(field.grid);
    const double alphaScale = std::clamp(opacity, 0.0, 1.0);

    // Column mapping cache: fractional grid column per output x (longitude only).
    std::vector<double> fxCol(static_cast<std::size_t>(view.width));
    std::vector<unsigned char> okCol(static_cast<std::size_t>(view.width), 0);
    for (int px = 0; px < view.width; ++px) {
        const double lon = tile::worldXToLon(view.topLeftWorldX + px + 0.5, view.zoom);
        double delta = lon - g.lon0;
        while (delta > 180.0) delta -= 360.0;
        while (delta <= -180.0) delta += 360.0;
        double fx = g.dlon != 0.0 ? delta / g.dlon : 0.0;
        bool ok = g.globalWrapLon ? true : (fx >= 0.0 && fx <= g.nlon - 1.0);
        if (g.globalWrapLon && g.nlon > 0) {
            const double n = g.nlon;
            fx = std::fmod(std::fmod(fx, n) + n, n);
        }
        fxCol[static_cast<std::size_t>(px)] = fx;
        okCol[static_cast<std::size_t>(px)] = ok ? 1 : 0;
    }

    // Row mapping cache: fractional grid row per output y (latitude only).
    std::vector<double> fyRow(static_cast<std::size_t>(view.height));
    std::vector<unsigned char> okRow(static_cast<std::size_t>(view.height), 0);
    for (int py = 0; py < view.height; ++py) {
        const double lat = tile::worldYToLat(view.topLeftWorldY + py + 0.5, view.zoom);
        const double fy = g.dlat != 0.0 ? (lat - g.lat0) / g.dlat : 0.0;
        fyRow[static_cast<std::size_t>(py)] = fy;
        okRow[static_cast<std::size_t>(py)] = (fy >= 0.0 && fy <= g.nlat - 1.0) ? 1 : 0;
    }

    auto fillRows = [&](int y0, int y1) {
        for (int py = y0; py < y1; ++py) {
            if (!okRow[static_cast<std::size_t>(py)]) continue;
            const double fy = fyRow[static_cast<std::size_t>(py)];
            auto* scan = reinterpret_cast<QRgb*>(img.scanLine(py));
            for (int px = 0; px < view.width; ++px) {
                if (!okCol[static_cast<std::size_t>(px)]) continue;
                const float v = sampleBilinear(field, fxCol[static_cast<std::size_t>(px)], fy);
                if (std::isnan(v)) continue;
                const Rgba c = cmap.map(v);
                if (c.a == 0) continue;
                const double a = (c.a / 255.0) * alphaScale;
                const auto A = static_cast<int>(std::lround(a * 255.0));
                // Premultiplied.
                const int R = static_cast<int>(std::lround(c.r * a));
                const int G = static_cast<int>(std::lround(c.g * a));
                const int B = static_cast<int>(std::lround(c.b * a));
                scan[px] = qRgba(R, G, B, A);
            }
        }
    };

    if (threads <= 1) {
        fillRows(0, view.height);
    } else {
        const int n = std::min(threads, view.height);
        std::vector<std::thread> pool;
        pool.reserve(static_cast<std::size_t>(n));
        const int chunk = (view.height + n - 1) / n;
        for (int t = 0; t < n; ++t) {
            const int y0 = t * chunk;
            const int y1 = std::min(y0 + chunk, view.height);
            if (y0 >= y1) break;
            pool.emplace_back(fillRows, y0, y1);
        }
        for (auto& th : pool) th.join();
    }
    return img;
}

}  // namespace met::render
