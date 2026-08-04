#pragma once

#include <optional>
#include <string>

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
// otherwise falls back to a magnitude heuristic (values > ~2000 are assumed Pa).
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

// Short label to show for a unit string ("Cel" -> "°C", "m/s" -> "m/s").
[[nodiscard]] std::string unitLabel(const std::string& units);

}  // namespace met::core
