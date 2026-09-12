#pragma once

#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "viewer/core/catalog.h"
#include "viewer/core/field.h"
#include "viewer/core/geo.h"
#include "viewer/core/timeaxis.h"

namespace met::analysis {

// A table of every selected variable's value down a vertical profile at one point.
//
// Sounding (sounding.h) is the same idea with a fixed set of six quantities, which
// is what a skew-T draws. This one carries a column set the user chooses at
// runtime, so the shape has to be a matrix rather than named fields.
//
// The other difference is how it is filled. extractSounding() takes all of its
// stacks at once, which is affordable because five is all it will ever be; here
// the caller sets the width, and holding six 40-level stacks of a mesoscale grid
// at once is gigabytes. So the profile is built empty from the *catalog* and each
// column is filled from one decoded stack that the caller can then drop.

// One level of a decoded stack: the level key (pressure in hPa on an isobaric
// axis, the model-level index on a native one) paired with its field. Matches
// what app/extractions.h's read* functions return.
using ProfileStack = std::vector<std::pair<double, core::Field2D>>;

// Why a cell has no number.
//
// sampleBilinear() returns NaN for "the point is outside this grid" and for "the
// data around the point is missing" alike, and a column whose variable simply has
// no record at this level is a third thing again. Printing one identical blank for
// all three tells the reader nothing about whether to move the point, pick a
// different variable, or distrust the file.
enum class CellStatus : std::uint8_t {
    Ok,             // a finite value
    NoRecord,       // this column's variable has no slab at this level
    OutsideGrid,    // the point lies outside this field's domain
    Missing,        // in domain, but the data there is missing
    Unconvertible,  // read, but its units could not be placed (height coordinate only)
};

enum class ProfileColumnKind : std::uint8_t {
    Variable,       // read straight out of the file
    WindSpeed,      // derived from the U/V pair
    WindDirection,  // derived from the U/V pair
};

// Synthetic ids for the derived columns. They share a namespace with catalog
// variable names, so they are spelled like the derived *fields* derived.cpp
// produces ("wspd", "wdir") rather than inventing a second vocabulary.
inline constexpr const char* kWindSpeedId = "wspd";
inline constexpr const char* kWindDirectionId = "wdir";

// One column: what it is, and the units its stored values are in. `id` is the
// catalog variable name for a read column and one of the constants above for a
// derived one. It is what the picker ticks and what settings persist, so it has
// to stay stable across sessions.
struct ProfileColumn {
    std::string id;
    std::string longName;
    std::string units;  // native units of the STORED values, never a display choice
    ProfileColumnKind kind = ProfileColumnKind::Variable;
};

// One row. `level` is the row's identity whatever the axis is; pressure and height
// are its two numeric vertical coordinates, always in hPa and gpm regardless of
// what the file used, so neither the table nor an export has to ask.
struct ProfileLevel {
    core::VerticalLevel level;
    double pressureHpa = std::numeric_limits<double>::quiet_NaN();
    float heightGpm = std::numeric_limits<float>::quiet_NaN();  // MSL geopotential height
    CellStatus pressureStatus = CellStatus::NoRecord;
    CellStatus heightStatus = CellStatus::NoRecord;
    std::vector<float> values;         // parallel to PointProfile::columns
    std::vector<CellStatus> statuses;  // parallel to values
};

struct PointProfile {
    core::LatLon point{};
    core::TimePoint validTime{};
    int member = -1;
    core::VerticalLevel::Type levelType = core::VerticalLevel::Type::Unknown;
    // True once any cell sampled inside a grid. The only honest way to tell "you
    // picked outside the data" apart from "the data there is missing", since
    // sampleBilinear collapses both to NaN.
    bool pointInDomain = false;
    // The variable heightGpm came from, empty when the file carries no height
    // field. Named in the export so the altitude column says where it came from.
    std::string heightSourceVar;
    std::vector<ProfileColumn> columns;
    std::vector<ProfileLevel> levels;
};

// What a dataset's catalog can offer a profile: which vertical axis, which rows,
// and which variables can be columns on it.
struct ProfileColumnChoices {
    core::VerticalLevel::Type levelType = core::VerticalLevel::Type::Unknown;
    std::vector<core::VerticalLevel> levels;  // the row set, ground first
    std::vector<ProfileColumn> available;     // offer these
    // Variables the file has that cannot be profile columns on this axis, kept so
    // the picker can show them disabled with a reason. Dropping them silently
    // would leave the reader wondering why a variable they can see on the map is
    // missing from the table.
    std::vector<ProfileColumn> unavailable;
    bool windAvailable = false;  // a U/V pair exists with >= 2 levels on this axis
    std::string heightVar;       // "" when the file carries no usable height field
};

// Pick the profile's vertical axis and sort its variables into what can and cannot
// be a column. Catalog-only: no I/O, so a picker and a progress estimate can both
// be built before anything is read.
//
// A variable needs at least two levels of the chosen axis to be a column -- with
// one, every cell but one would read NoRecord, which is a column of blanks rather
// than a profile. That is the same bar findHeightVariable() already applies.
[[nodiscard]] ProfileColumnChoices profileColumnChoices(
    const std::vector<core::VariableEntry>& vars);

// Build the empty table: one row per level, one cell per column, every cell
// NoRecord until filled. Rows come out **ground first** -- highest pressure and
// lowest altitude at the top. That is the opposite of Sounding, which is ordered
// top-down to match how a skew-T is drawn; a table of site conditions is read
// from the ground up, so the default follows the reader rather than the sibling
// struct. The view can sort it either way.
[[nodiscard]] PointProfile makeProfile(const std::vector<core::VerticalLevel>& levels,
                                       const std::vector<ProfileColumn>& columns,
                                       core::LatLon point);

// Sample one decoded stack into column `columnIndex`. Stack entries are matched to
// rows by the level's own value, which is the pressure on an isobaric axis and the
// model-level index on a native one -- one rule, no path split here. `stack` may be
// destroyed as soon as this returns, which is what keeps peak memory to one column.
void fillProfileColumn(PointProfile& p, std::size_t columnIndex, const ProfileStack& stack);

// Fill every WindSpeed/WindDirection column from one U/V pair, so the pair is read
// and held once however many derived wind columns are on. Uses
// earthRelativeWindAt(), so components that are grid-relative on a projected grid
// are rotated before the speed and direction are taken.
void fillProfileWind(PointProfile& p, const ProfileStack& u, const ProfileStack& v);

// Fill the MSL height coordinate, converting each sample through its own field's
// units. A level whose units cannot be placed reads Unconvertible rather than
// carrying a wrong altitude.
void fillProfileHeights(PointProfile& p, const ProfileStack& z, const std::string& sourceVar);

// Fill the pressure coordinate from a `pres` stack, for native model levels.
// On an isobaric axis makeProfile() has already set it from the level value.
void fillProfilePressures(PointProfile& p, const ProfileStack& pres);

}  // namespace met::analysis
