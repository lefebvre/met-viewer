#include <gtest/gtest.h>

#include <cmath>

#include "viewer/render/tilemath.h"

using namespace met::render::tile;

TEST(TileMath, WorldSize) {
    EXPECT_DOUBLE_EQ(worldSize(0), 256.0);
    EXPECT_DOUBLE_EQ(worldSize(1), 512.0);
    EXPECT_DOUBLE_EQ(worldSize(10), 256.0 * 1024.0);
}

TEST(TileMath, OriginAndCenter) {
    // lon -180 -> x 0; lon +180 -> x worldSize.
    EXPECT_NEAR(lonToWorldX(-180.0, 4), 0.0, 1e-9);
    EXPECT_NEAR(lonToWorldX(180.0, 4), worldSize(4), 1e-9);
    // lon 0 -> center x.
    EXPECT_NEAR(lonToWorldX(0.0, 4), worldSize(4) / 2.0, 1e-9);
    // lat 0 -> center y (equator is the vertical middle of the Mercator world).
    EXPECT_NEAR(latToWorldY(0.0, 4), worldSize(4) / 2.0, 1e-9);
}

TEST(TileMath, RoundTrip) {
    for (int zoom : {0, 3, 8, 14}) {
        for (double lon : {-179.0, -90.0, 0.0, 45.5, 179.0}) {
            for (double lat : {-80.0, -33.3, 0.0, 51.5, 80.0}) {
                const double x = lonToWorldX(lon, zoom);
                const double y = latToWorldY(lat, zoom);
                EXPECT_NEAR(worldXToLon(x, zoom), lon, 1e-6) << "z" << zoom << " lon" << lon;
                EXPECT_NEAR(worldYToLat(y, zoom), lat, 1e-6) << "z" << zoom << " lat" << lat;
            }
        }
    }
}

TEST(TileMath, LatitudeClamp) {
    // Beyond the Mercator limit, y saturates rather than diverging.
    const double yHi = latToWorldY(89.0, 5);
    const double yClamp = latToWorldY(kMaxLat, 5);
    EXPECT_NEAR(yHi, yClamp, 1e-6);
    EXPECT_TRUE(std::isfinite(yHi));
}

TEST(TileMath, KnownTileAtZoom1) {
    // At zoom 1 the world is 2x2 tiles. London (~ -0.13, 51.5) sits just west of
    // the prime meridian and north of the equator: top-left tile (x=0, y=0),
    // i.e. world x just below center, y above center.
    const int z = 1;
    EXPECT_LT(lonToWorldX(-0.13, z), worldSize(z) / 2.0);
    EXPECT_LT(latToWorldY(51.5, z), worldSize(z) / 2.0);
    // A point just east of the meridian lands in the right tile.
    EXPECT_GT(lonToWorldX(0.13, z), worldSize(z) / 2.0);
}

TEST(TileMath, ZoomForSpan) {
    // A 1-degree span in a 1000px view fits at a high zoom; a 360-degree span
    // fits only at zoom 0-1 in the same width.
    EXPECT_GT(zoomForLonSpan(1.0, 1000), zoomForLonSpan(180.0, 1000));
    EXPECT_LE(zoomForLonSpan(360.0, 256), 0);
}
