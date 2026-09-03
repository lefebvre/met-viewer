#include <gtest/gtest.h>

#include <cmath>
#include <iostream>
#include <numbers>

#include "viewer/core/crs.h"
#include "viewer/core/geo.h"

using namespace met::core;

// Absolute georeferencing of the spherical grids.
//
// GRIB and ARL both define conformal grids on a sphere (R = 6371200 m), while the
// basemap tiles underneath them are Web Mercator over WGS84. Every other test in
// this suite checks that the readers' own math is self-consistent; these check the
// part that self-consistency cannot: that the projection PROJ performs is the one
// the format intends, and that the sphere/WGS84 mismatch does not move the data
// relative to the coastlines drawn under it.
namespace {

constexpr double kArlR = 6371200.0;
constexpr double kTangLat = 38.5;  // HRRR-like tangent latitude
constexpr double kRefLon = 262.5;  // = -97.5

const double kDeg = std::numbers::pi / 180.0;

std::string lccSphere() {
    return "+proj=lcc +lat_1=38.5 +lat_2=38.5 +lat_0=38.5 +lon_0=262.5 +R=6371200 +units=m "
           "+no_defs";
}

std::string lccWgs84() {
    return "+proj=lcc +lat_1=38.5 +lat_2=38.5 +lat_0=38.5 +lon_0=262.5 +ellps=WGS84 +units=m "
           "+no_defs";
}

// Closed-form spherical Lambert conformal conic, tangent case (Snyder 15-1..15-4
// with the spherical 14-x forms), written out independently of PROJ so that the
// comparison below is a real check and not a tautology.
void snyderLcc(double lonDeg, double latDeg, double& x, double& y) {
    const double phi0 = kTangLat * kDeg;
    const double phi = latDeg * kDeg;
    const double n = std::sin(phi0);
    const double F = std::cos(phi0) * std::pow(std::tan(std::numbers::pi / 4 + phi0 / 2), n) / n;
    const double rho = kArlR * F / std::pow(std::tan(std::numbers::pi / 4 + phi / 2), n);
    const double rho0 = kArlR * F / std::pow(std::tan(std::numbers::pi / 4 + phi0 / 2), n);
    const double theta = n * (wrapLon180(lonDeg - kRefLon) * kDeg);
    x = rho * std::sin(theta);
    y = rho0 - rho * std::cos(theta);
}

struct Place {
    const char* name;
    double lat, lon;
};

// Coastal/known points spread across a CONUS-sized domain.
constexpr Place kPlaces[] = {
    {"Miami", 25.7617, -80.1918},    {"Seattle", 47.6062, -122.3321},
    {"New York", 40.7128, -74.0060}, {"Corpus Christi", 27.8006, -97.3964},
    {"Duluth", 46.7867, -92.1005},
};

}  // namespace

// If PROJ were quietly applying an ellipsoid or a datum shift to the +R= string,
// the result would diverge from the spherical closed form by kilometres. Agreeing
// to well under a metre is what says the grid is projected on the sphere the
// format declares, with the latitudes taken as-is.
TEST(CrsSphere, ProjMatchesClosedFormSphericalLcc) {
    const Crs crs(lccSphere());
    for (const Place& p : kPlaces) {
        double px = 0, py = 0;
        ASSERT_TRUE(crs.forward(p.lon, p.lat, px, py)) << p.name;
        double sx = 0, sy = 0;
        snyderLcc(p.lon, p.lat, sx, sy);
        EXPECT_NEAR(px, sx, 1.0) << p.name;
        EXPECT_NEAR(py, sy, 1.0) << p.name;
    }
}

// Round-tripping through the sphere must return the same geographic coordinate,
// i.e. no datum conversion is inserted in either direction.
TEST(CrsSphere, ForwardInverseRoundTripsExactly) {
    const Crs crs(lccSphere());
    for (const Place& p : kPlaces) {
        double px = 0, py = 0;
        ASSERT_TRUE(crs.forward(p.lon, p.lat, px, py)) << p.name;
        double lon = 0, lat = 0;
        ASSERT_TRUE(crs.inverse(px, py, lon, lat)) << p.name;
        EXPECT_NEAR(wrapLon180(lon), wrapLon180(p.lon), 1e-9) << p.name;
        EXPECT_NEAR(lat, p.lat, 1e-9) << p.name;
    }
}

// The question this file exists to settle: does defining the grid on a sphere
// rather than WGS84 shift the data relative to the WGS84 basemap under it?
//
// A grid is anchored by projecting one known sync lat/lon and stepping out in
// fixed metre increments, so the anchor error cancels by construction and what is
// left is the difference in *scale* over the domain. This measures that directly:
// walk 1500 km (a HRRR half-diagonal) from the anchor in both projections and
// compare where the endpoint lands geographically.
TEST(CrsSphere, SphereVersusWgs84DisplacementOverConusDomain) {
    const Crs sphere(lccSphere());
    const Crs wgs84(lccWgs84());

    // Anchor both grids at the same geographic sync point.
    const double syncLat = 38.5, syncLon = 262.5;
    double sax = 0, say = 0, wax = 0, way = 0;
    ASSERT_TRUE(sphere.forward(syncLon, syncLat, sax, say));
    ASSERT_TRUE(wgs84.forward(syncLon, syncLat, wax, way));

    double worstKm = 0.0;
    for (const double dx : {-1500e3, 0.0, 1500e3}) {
        for (const double dy : {-1500e3, 0.0, 1500e3}) {
            double slon = 0, slat = 0, wlon = 0, wlat = 0;
            ASSERT_TRUE(sphere.inverse(sax + dx, say + dy, slon, slat));
            ASSERT_TRUE(wgs84.inverse(wax + dx, way + dy, wlon, wlat));
            worstKm = std::max(worstKm, greatCircleKm(LatLon{slat, wrapLon180(slon)},
                                                      LatLon{wlat, wrapLon180(wlon)}));
        }
    }

    // Printed rather than only asserted: the point of this test is to put a number
    // on the mismatch, so the number belongs in the output where a future reader
    // can see it without re-deriving it.
    std::cout << "sphere vs WGS84 worst-corner displacement: " << worstKm << " km\n";

    // Measured 5.3 km at the corners of a 3000 km box (PROJ 9). That is ~1 screen
    // pixel at zoom 7 and invisible at the zooms a synoptic field is viewed at,
    // but it is not zero — so substituting WGS84 for the declared sphere would put
    // a HRRR domain's edge visibly off its coastline. The bounds are a tripwire on
    // that reasoning, loose enough to survive PROJ version drift.
    EXPECT_LT(worstKm, 8.0);
    EXPECT_GT(worstKm, 1.0) << "expected a measurable sphere/WGS84 difference; if this "
                               "vanished, PROJ may be silently harmonizing the datums";
}
