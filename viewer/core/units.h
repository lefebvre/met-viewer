#pragma once

#include <optional>
#include <string>
#include <vector>

namespace met::core {

// Convert a scalar between two units. Returns nullopt if the pair is unknown.
// Supported (both directions): K<->Cel, Pa<->hPa, m/s<->kt, gpm<->dam,
// m2/s2<->gpm/dam (geopotential <-> geopotential height, via g = 9.80665),
// kg/kg<->g/kg, m<->mm. Unit strings are matched case-insensitively against a
// few common spellings. A dimensionless "1" is deliberately NOT treated as a
// mixing ratio — it is used for masks and fractions too.
[[nodiscard]] std::optional<double> convert(double value, const std::string& from,
                                            const std::string& to);

// Convert a pressure sample to hPa. Uses convert() when `units` is recognized,
// otherwise falls back to a magnitude heuristic (values > ~2000 are assumed Pa)
// and warns once per unrecognized unit, so a mislabelled field leaves a trace
// instead of quietly setting the scale of a pressure axis. Contrast
// toGeopotentialMeters below, which has no heuristic at all — see why there.
// Convenience for readers/analysis that receive pressure in Pa or hPa.
[[nodiscard]] double toHpa(double value, const std::string& units);

// Convert a height/geopotential sample to geopotential metres (gpm), for the
// height axis of a sounding or cross-section. Handles gpm/dam and geopotential
// (m2/s2, divided by g); geometric metres are taken as gpm, which they match to
// better than 0.5% through the whole troposphere — far below what a plot shows.
// Returns NaN for units it cannot place, so a mislabelled field reads as "no
// height" rather than as a wrong altitude. There is deliberately no magnitude
// heuristic here: 5000 is a plausible sample in gpm *and* in m2/s2.
[[nodiscard]] double toGeopotentialMeters(double value, const std::string& units);

// A friendlier display alternative for a native unit, if one exists (e.g. "K"
// -> "Cel"). Returns nullopt when the native unit is already the sensible one.
[[nodiscard]] std::optional<std::string> preferredDisplayUnit(const std::string& units);

// Every unit a value can be shown as, canonical native unit first, for a UI that
// offers the reader a choice. Wind speed is the motivating case -- m/s and knots
// are both routine and neither is the obvious default -- but temperature,
// pressure and height have the same property, so this is a general lookup rather
// than a special case for speed.
//
// The families it reports are exactly the ones convert() implements: offering a
// unit convert() cannot reach would be a menu entry that silently produces
// nothing. A unit with no alternative yields a single entry, so a caller can
// always build a menu from the result without special-casing.
[[nodiscard]] std::vector<std::string> alternativeUnits(const std::string& units);

// A unit string in the one spelling this app shows, whatever the file used:
// factors separated by spaces, one slash before everything with a negative
// exponent, and parentheses when more than one factor sits below it. So
// "m s**-1", "m s-1" and "m/s" all read "m/s", "kg m-2 s-1" reads "kg/(m² s)",
// and "s**-1" reads "1/s". "Cel" reads "°C". The shortest spelling that cannot
// be misread, since "kg/m²/s" can.
//
// A string that does not parse as units and exponents -- "(10**-6 g) m**-3",
// "(0 - 1)", "1" -- comes back unchanged: a label is only rewritten when the
// rewrite provably says the same thing.
[[nodiscard]] std::string unitLabel(const std::string& units);

// unitLabel() in plain ASCII, for exported files: exponents as trailing digits
// ("kg/(m2 s)") and "degC" for Celsius, the spellings UDUNITS and pint parse.
[[nodiscard]] std::string unitLabelAscii(const std::string& units);

}  // namespace met::core
