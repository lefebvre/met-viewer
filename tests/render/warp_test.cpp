#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <cstdio>

#include "viewer/core/field.h"
#include "viewer/render/colormap.h"
#include "viewer/render/tilemath.h"
#include "viewer/render/warp.h"

using namespace met;
using namespace met::render;

namespace {
// Linear field value = 273 + 0.1*lon - 0.2*lat over a regular grid.
core::Field2D linearField(int nlon, int nlat, double lat0, double lon0, double dlat, double dlon) {
    core::RegularLatLonGrid g;
    g.lat0 = lat0; g.lon0 = lon0; g.dlat = dlat; g.dlon = dlon;
    g.nlon = nlon; g.nlat = nlat;
    core::Field2D f;
    f.grid = g;
    f.values.resize(static_cast<std::size_t>(nlon) * static_cast<std::size_t>(nlat));
    for (int r = 0; r < nlat; ++r)
        for (int c = 0; c < nlon; ++c) {
            const double lat = lat0 + dlat * r;
            const double lon = lon0 + dlon * c;
            f.values[static_cast<std::size_t>(r) * static_cast<std::size_t>(nlon) +
                     static_cast<std::size_t>(c)] =
                static_cast<float>(273.0 + 0.1 * lon - 0.2 * lat);
        }
    return f;
}

MercatorViewport viewportFor(const core::BBox& b, int width, int height, int zoom) {
    const double cx = tile::lonToWorldX(0.5 * (b.minLon + b.maxLon), zoom);
    const double cy = tile::latToWorldY(0.5 * (b.minLat + b.maxLat), zoom);
    return {cx - width / 2.0, cy - height / 2.0, zoom, width, height};
}
}  // namespace

TEST(Warp, ValueMatchesColormapAtPixel) {
    auto f = linearField(16, 8, 70, 0, -2, 2);       // 0..30 lon, 56..70 lat
    auto cmap = Colormap::builtin("viridis");
    cmap.setRange(259.0, 265.0);

    const core::BBox b = core::gridBBox(f.grid);
    const int z = tile::zoomForLonSpan(b.maxLon - b.minLon, 400);
    MercatorViewport view = viewportFor(b, 400, 300, z);

    QImage img = warpToMercator(f, cmap, view, 1.0, 1);
    ASSERT_EQ(img.width(), 400);

    // Pick a pixel near the center; invert to lon/lat; expected value & color.
    const int px = 200, py = 150;
    const double lon = tile::worldXToLon(view.topLeftWorldX + px + 0.5, z);
    const double lat = tile::worldYToLat(view.topLeftWorldY + py + 0.5, z);
    const double expected = 273.0 + 0.1 * lon - 0.2 * lat;
    const Rgba c = cmap.map(expected);

    const QRgb got = img.pixel(px, py);
    ASSERT_EQ(qAlpha(got), 255);
    EXPECT_NEAR(qRed(got), c.r, 4);
    EXPECT_NEAR(qGreen(got), c.g, 4);
    EXPECT_NEAR(qBlue(got), c.b, 4);
}

TEST(Warp, OutsideDomainIsTransparent) {
    auto f = linearField(16, 8, 70, 0, -2, 2);
    auto cmap = Colormap::builtin("viridis");
    cmap.setRange(259.0, 265.0);
    // Zoom far in so the small field covers only part of a large viewport.
    const core::BBox b = core::gridBBox(f.grid);
    MercatorViewport view = viewportFor(b, 600, 600, tile::zoomForLonSpan(b.maxLon - b.minLon, 100));
    QImage img = warpToMercator(f, cmap, view, 1.0, 1);
    // Corners of a viewport larger than the field must be transparent.
    EXPECT_EQ(qAlpha(img.pixel(0, 0)), 0);
    EXPECT_EQ(qAlpha(img.pixel(img.width() - 1, img.height() - 1)), 0);
}

TEST(Warp, OpacityScalesAlpha) {
    auto f = linearField(16, 8, 70, 0, -2, 2);
    auto cmap = Colormap::builtin("viridis");
    cmap.setRange(259.0, 265.0);
    const core::BBox b = core::gridBBox(f.grid);
    const int z = tile::zoomForLonSpan(b.maxLon - b.minLon, 400);
    MercatorViewport view = viewportFor(b, 400, 300, z);
    QImage img = warpToMercator(f, cmap, view, 0.5, 1);
    EXPECT_NEAR(qAlpha(img.pixel(200, 150)), 128, 3);
}

TEST(Warp, SingleAndMultiThreadedAgree) {
    auto f = linearField(64, 48, 80, -40, -2, 2);
    auto cmap = Colormap::builtin("turbo");
    cmap.setRange(200.0, 300.0);
    const core::BBox b = core::gridBBox(f.grid);
    const int z = tile::zoomForLonSpan(b.maxLon - b.minLon, 800);
    MercatorViewport view = viewportFor(b, 800, 600, z);
    QImage a = warpToMercator(f, cmap, view, 1.0, 1);
    QImage b2 = warpToMercator(f, cmap, view, 1.0, 4);
    ASSERT_EQ(a.size(), b2.size());
    EXPECT_EQ(a, b2);  // identical pixels regardless of thread count
}

// Performance tripwire: a 1080p warp of an ERA5-sized regular grid must stay
// well under budget. Generous threshold to avoid flakiness while still catching
// gross regressions (e.g. accidentally dropping the per-row mapping cache).
TEST(Warp, PerformanceTripwire) {
    auto f = linearField(1440, 721, 90, -180, -0.25, 0.25);
    auto cmap = Colormap::builtin("viridis");
    cmap.setRange(200.0, 320.0);
    const core::BBox b = core::gridBBox(f.grid);
    MercatorViewport view = viewportFor(b, 1920, 1080, tile::zoomForLonSpan(360.0, 1920));

    // Warm up, then time the single-threaded path.
    (void)warpToMercator(f, cmap, view, 1.0, 1);
    const auto t0 = std::chrono::steady_clock::now();
    QImage img = warpToMercator(f, cmap, view, 1.0, 1);
    const auto t1 = std::chrono::steady_clock::now();
    const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    std::printf("[warp] 1920x1080 single-threaded: %.2f ms\n", ms);
    ASSERT_FALSE(img.isNull());
    EXPECT_LT(ms, 150.0);
}
