#pragma once

#include <cstddef>
#include <string>

namespace met::core {

// Transforms between geographic coordinates (lon/lat in degrees, EPSG:4326) and
// a projected coordinate system given by a proj4/PROJ string (e.g. a Lambert
// conformal conic). Holds only the projection string by value; the underlying
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
