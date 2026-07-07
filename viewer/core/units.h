#pragma once

#include <optional>
#include <string>

namespace met::core {

// Convert a scalar between two units. Returns nullopt if the pair is unknown.
// Supported (both directions): K<->Cel, Pa<->hPa, m/s<->kt, gpm<->dam,
// kg/kg<->g/kg, m<->mm. Unit strings are matched case-insensitively against a
// few common spellings.
[[nodiscard]] std::optional<double> convert(double value, const std::string& from,
                                            const std::string& to);

// A friendlier display alternative for a native unit, if one exists (e.g. "K"
// -> "Cel"). Returns nullopt when the native unit is already the sensible one.
[[nodiscard]] std::optional<std::string> preferredDisplayUnit(const std::string& units);

// Short label to show for a unit string ("Cel" -> "°C", "m/s" -> "m/s").
[[nodiscard]] std::string unitLabel(const std::string& units);

}  // namespace met::core
