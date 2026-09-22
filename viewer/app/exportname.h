#pragma once

#include <QString>

#include "viewer/core/field.h"
#include "viewer/core/geo.h"

namespace met::analysis {
struct CrossSection;
struct Sounding;
struct TimeSeries;
}  // namespace met::analysis

namespace met::app {

// The pieces an export's default file name is built from, spelled once so that a
// CSV and a PDF of the same place and time name it the same way.

// "33.50S_70.12W": hemisphere letters rather than signs, because a "-" in the
// middle of a name reads as a separator, not as part of the number.
[[nodiscard]] QString latLonTag(core::LatLon p);

// "500hPa", "2m", "ml12", "surface": the level with its unit and without spaces.
[[nodiscard]] QString levelTag(const core::VerticalLevel& lvl);

// `s` with every character a file name cannot safely carry replaced by "-".
// Variable names come from the file, and a GRIB or NetCDF name is under no
// obligation to avoid "/" or ":".
[[nodiscard]] QString fileSafe(const QString& s);

// Default names, without the extension, for a figure of each view: what it shows,
// where, and when, taken from the data the view is drawing rather than from the
// time slider, which can be ahead of it while a refresh is still extracting. A view
// with nothing to draw gets the bare kind ("plot", "skewt", ...).
[[nodiscard]] QString plotFigureStem(const core::Field2D* field);
[[nodiscard]] QString soundingFigureStem(const analysis::Sounding& s);
[[nodiscard]] QString sectionFigureStem(const analysis::CrossSection& cs);
[[nodiscard]] QString seriesFigureStem(const analysis::TimeSeries& ts);

}  // namespace met::app
