#pragma once

#include <cstddef>
#include <string>

namespace met::core {

// Register an additional directory to search for PROJ's data files (proj.db).
// Used only when neither PROJ_DATA nor PROJ_LIB is set in the environment, and
// it takes precedence over the compile-time MET_PROJ_DATA fallback. Intended to
// be called once at startup with a path resolved relative to the executable, so
// installed/bundled builds (AppImage, Windows installer) locate proj.db without
// depending on the build-machine layout. Safe to call before any Crs is used.
void setProjDataPath(std::string path);

// Transforms between geographic coordinates (lon/lat in degrees, EPSG:4326) and
// a projected coordinate system given by a proj4/PROJ string (e.g. a Lambert
// conformal conic).
//
// On the sphere/WGS84 question: GRIB and ARL declare their conformal grids on a
// sphere (R = 6371200 m) while the basemap tiles are WGS84, and PROJ does *not*
// insert a datum conversion between them — it reprojects the same angular
// coordinates on a different radius, which is the meteorological convention these
// formats are defined under. Verified in tests/core/crs_sphere_test.cpp against
// the closed-form spherical projection (agreement < 1 m) and measured against a
// WGS84 ellipsoid of the same projection: 5.3 km at the corners of a 3000 km
// domain. So: build the CRS from the sphere the file declares and never
// substitute WGS84 for it, but do not expect a visible basemap offset from the
// mismatch either.
//
// Holds only the projection string by value; the underlying
// PROJ objects are created lazily and cached per-thread, so a Crs is cheap to
// copy and safe to use concurrently from multiple threads (each thread builds
// its own PJ). Use the batch forms in hot loops (warp).
class Crs {
public:
    Crs() = default;
    explicit Crs(std::string projString) : proj_(std::move(projString)) {}

    [[nodiscard]] const std::string& proj() const { return proj_; }
    [[nodiscard]] bool empty() const { return proj_.empty(); }

    // lon/lat (deg) -> projected x/y (meters). Returns false on failure.
    [[nodiscard]] bool forward(double lon, double lat, double& x, double& y) const;
    // projected x/y (meters) -> lon/lat (deg). Returns false on failure.
    [[nodiscard]] bool inverse(double x, double y, double& lon, double& lat) const;

    // In-place batch transforms over n points (arrays of size n). On failure a
    // point is left as HUGE_VAL. forwardBatch: (lon,lat)->(x,y); inverseBatch:
    // (x,y)->(lon,lat).
    void forwardBatch(double* a, double* b, std::size_t n) const;
    void inverseBatch(double* a, double* b, std::size_t n) const;

private:
    std::string proj_;
};

}  // namespace met::core
