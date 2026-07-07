#pragma once

#include <cstddef>
#include <string>
#include <variant>

#include "viewer/core/crs.h"
#include "viewer/core/geo.h"

namespace met::core {

// A regular latitude/longitude grid (GRIB regular_ll, ERA5 NetCDF). Increments
// are signed: a north-to-south scan has dlat < 0. The first grid point is
// (lat0, lon0) at index (0, 0); column index runs along longitude, row index
// along latitude.
struct RegularLatLonGrid {
    double lat0 = 0.0;
    double lon0 = 0.0;
    double dlat = 0.0;  // signed spacing between rows
    double dlon = 0.0;  // signed spacing between columns
    int nlon = 0;       // columns (Ni)
    int nlat = 0;       // rows (Nj)
    bool globalWrapLon = false;
};

// A grid defined in a projected coordinate system (GRIB Lambert / polar-
// stereographic, conformal ARL). Grid point (0, 0) is at projected coordinate
// (x0, y0) in metres; dx/dy are signed spacings; the projection is given by a
// proj4 string. Geographic <-> index mapping goes through PROJ.
struct ProjectedGrid {
    Crs crs;
    double x0 = 0.0;
    double y0 = 0.0;
    double dx = 0.0;
    double dy = 0.0;
    int nx = 0;
    int ny = 0;
};

// The internal grid abstraction. Every reader normalizes into one of these
// alternatives; everything above (warp, sampling, contours) only speaks GridDef.
using GridDef = std::variant<RegularLatLonGrid, ProjectedGrid>;

// Number of columns / rows, and total cells.
[[nodiscard]] int gridWidth(const GridDef& g);
[[nodiscard]] int gridHeight(const GridDef& g);
[[nodiscard]] std::size_t gridCount(const GridDef& g);

// Map a geographic coordinate to a fractional grid index (column x, row y).
// Sets GridIndex::inDomain=false when the point lies outside the grid.
[[nodiscard]] GridIndex latlonToIndex(const GridDef& g, LatLon ll);

// Inverse of latlonToIndex for integer or fractional indices.
[[nodiscard]] LatLon indexToLatLon(const GridDef& g, double x, double y);

// Geographic bounding box covered by the grid.
[[nodiscard]] BBox gridBBox(const GridDef& g);

}  // namespace met::core
