#pragma once

#include <cstddef>
#include <variant>

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

// The internal grid abstraction. Every reader normalizes into one of these
// alternatives; everything above (warp, sampling, contours) only speaks GridDef.
// M1 ships RegularLatLonGrid; GaussianGrid and ProjectedGrid are added later.
using GridDef = std::variant<RegularLatLonGrid>;

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
