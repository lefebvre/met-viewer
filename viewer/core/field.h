#pragma once

#include <string>
#include <vector>

#include "viewer/core/grid.h"
#include "viewer/core/timeaxis.h"

namespace met::core {

// Identifies one 2D slab within a dataset.
struct FieldKey {
    std::string varName;                  // canonical short name ("t", "u", "gh")
    VerticalLevel level;                  // vertical position
    TimePoint validTime;                  // valid time (UTC)
    int member = -1;                      // ensemble member, -1 = deterministic

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
};

// A decoded 2D field. Values are row-major (row 0 first), missing entries are
// quiet NaN (normalized at decode time). `grid` fully describes the geometry.
struct Field2D {
    GridDef grid;
    std::vector<float> values;
    FieldMeta meta;

    [[nodiscard]] int width() const { return gridWidth(grid); }
    [[nodiscard]] int height() const { return gridHeight(grid); }

    // Value at integer (col, row) with no bounds checking.
    [[nodiscard]] float at(int col, int row) const {
        return values[static_cast<std::size_t>(row) * static_cast<std::size_t>(width()) +
                      static_cast<std::size_t>(col)];
    }
};

}  // namespace met::core
