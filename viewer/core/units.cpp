#include "viewer/core/units.h"

#include <algorithm>
#include <cctype>

namespace met::core {
namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// Normalize common spellings to a canonical token.
std::string canon(const std::string& units) {
    const std::string u = lower(units);
    if (u == "k" || u == "kelvin") return "K";
    if (u == "c" || u == "cel" || u == "celsius" || u == "degc" || u == "deg_c" || u == "°c")
        return "Cel";
    if (u == "pa" || u == "pascal") return "Pa";
    if (u == "hpa" || u == "millibar" || u == "mb" || u == "mbar") return "hPa";
    if (u == "m/s" || u == "m s-1" || u == "m s**-1" || u == "meter/second") return "m/s";
    if (u == "kt" || u == "knot" || u == "knots") return "kt";
    if (u == "gpm" || u == "geopotential meter" || u == "gpdm") return "gpm";
    // Geopotential is NOT geopotential height: Phi [m2/s2] = g * z [gpm]. Keeping
    // them as distinct canonical units means a value in m2/s2 is divided by g on
    // the way to gpm/dam instead of being silently mislabelled by a factor of ~9.81.
    if (u == "m2/s2" || u == "m**2 s**-2" || u == "m2 s-2" || u == "m^2/s^2") return "m2/s2";
    if (u == "dam" || u == "decameter" || u == "dm") return "dam";
    // Deliberately no "1" here: a dimensionless unit is used for mixing ratios,
    // fractional RH, land-sea masks and more, so mapping it to kg/kg would offer a
    // bogus x1000 conversion on unrelated fields.
    if (u == "kg/kg" || u == "kg kg-1" || u == "kg kg**-1") return "kg/kg";
    if (u == "g/kg") return "g/kg";
    if (u == "m" || u == "meter" || u == "metre") return "m";
    if (u == "mm" || u == "millimeter" || u == "millimetre") return "mm";
    return units;
}

}  // namespace

std::optional<double> convert(double value, const std::string& from, const std::string& to) {
    const std::string f = canon(from);
    const std::string t = canon(to);
    if (f == t) return value;

    if (f == "K" && t == "Cel") return value - 273.15;
    if (f == "Cel" && t == "K") return value + 273.15;

    if (f == "Pa" && t == "hPa") return value * 1e-2;
    if (f == "hPa" && t == "Pa") return value * 100.0;

    if (f == "m/s" && t == "kt") return value * 1.9438444924406;
    if (f == "kt" && t == "m/s") return value / 1.9438444924406;

    if (f == "gpm" && t == "dam") return value * 1e-1;
    if (f == "dam" && t == "gpm") return value * 10.0;

    // Geopotential <-> geopotential height, via standard gravity (WMO g = 9.80665).
    constexpr double kG = 9.80665;
    if (f == "m2/s2" && t == "gpm") return value / kG;
    if (f == "gpm" && t == "m2/s2") return value * kG;
    if (f == "m2/s2" && t == "dam") return value / (kG * 10.0);
    if (f == "dam" && t == "m2/s2") return value * kG * 10.0;

    if (f == "kg/kg" && t == "g/kg") return value * 1000.0;
    if (f == "g/kg" && t == "kg/kg") return value * 1e-3;

    if (f == "m" && t == "mm") return value * 1000.0;
    if (f == "mm" && t == "m") return value * 1e-3;

    return std::nullopt;
}

double toHpa(double value, const std::string& units) {
    if (const auto c = convert(value, units, "hPa")) return *c;
    return value > 2000.0 ? value * 1e-2 : value;  // ~Pa vs hPa
}

std::optional<std::string> preferredDisplayUnit(const std::string& units) {
    const std::string u = canon(units);
    if (u == "K") return std::string("Cel");
    if (u == "Pa") return std::string("hPa");
    // Geopotential is almost always read as a height; show the gpm equivalent.
    if (u == "m2/s2") return std::string("gpm");
    return std::nullopt;
}

std::string unitLabel(const std::string& units) {
    const std::string u = canon(units);
    if (u == "Cel") return "°C";  // °C
    if (u == "K") return "K";
    return units;
}

}  // namespace met::core
