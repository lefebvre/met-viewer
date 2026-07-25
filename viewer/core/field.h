#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "viewer/core/grid.h"
#include "viewer/core/timeaxis.h"

namespace met::core {

// Identifies one 2D slab within a dataset.
struct FieldKey {
    std::string varName;  // canonical short name ("t", "u", "gh")
    VerticalLevel level;  // vertical position
    TimePoint validTime;  // valid time (UTC)
    int member = -1;      // ensemble member, -1 = deterministic

    friend bool operator==(const FieldKey& a, const FieldKey& b) {
        return a.varName == b.varName && a.level == b.level && a.validTime == b.validTime &&
               a.member == b.member;
    }
};

// Descriptive metadata attached to a decoded field.
struct FieldMeta {
    std::string varName;       // canonical short name
    std::string longName;      // human-readable ("Temperature")
    std::string units;         // native units string ("K")
    std::string standardName;  // CF standard_name if known ("air_temperature")
    VerticalLevel level;
    TimePoint validTime;
    // For wind components: true when u/v are resolved along the grid axes
    // (grid-relative) rather than east/north (earth-relative).
    bool gridRelativeWind = false;
};

// A process-unique id handed to each newly constructed Field2D. Views cache
// expensive derived products (the warped raster, marching-squares isolines, a GPU
// texture) and need to know when the field under them changed. Comparing the
// Field2D's *address* almost works and fails silently: free one field, allocate
// the next, and the allocator can hand back the same address, leaving a stale
// raster on screen with nothing to indicate it. A monotonic counter cannot alias.
[[nodiscard]] std::uint64_t nextFieldId();

// A decoded 2D field. Values are row-major (row 0 first), missing entries are
// quiet NaN (normalized at decode time). `grid` fully describes the geometry.
//
// `id` is fresh per constructed object and copied along with the values, so a
// copy compares equal to its source (same content, same derived products). Code
// that mutates `values` in place must assign a new id — see rotateToEarthRelative.
struct Field2D {
    GridDef grid;
    std::vector<float> values;
    FieldMeta meta;
    std::uint64_t id = nextFieldId();

    [[nodiscard]] int width() const { return gridWidth(grid); }
    [[nodiscard]] int height() const { return gridHeight(grid); }

    // Value at integer (col, row) with no bounds checking.
    [[nodiscard]] float at(int col, int row) const {
        return values[static_cast<std::size_t>(row) * static_cast<std::size_t>(width()) +
                      static_cast<std::size_t>(col)];
    }
};

}  // namespace met::core
