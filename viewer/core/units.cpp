#include "viewer/core/units.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <mutex>
#include <optional>
#include <set>
#include <utility>
#include <vector>

#include "viewer/core/log.h"

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
    if (u == "gpm" || u == "geopotential meter" || u == "gpmeter") return "gpm";
    // Geopotential is NOT geopotential height: Phi [m2/s2] = g * z [gpm]. Keeping
    // them as distinct canonical units means a value in m2/s2 is divided by g on
    // the way to gpm/dam instead of being silently mislabelled by a factor of ~9.81.
    if (u == "m2/s2" || u == "m**2 s**-2" || u == "m2 s-2" || u == "m^2/s^2") return "m2/s2";
    // "gpdm" is a geopotential *decametre* — the unit 500 hPa charts are drawn in.
    // It belongs with dam, not gpm: reading it as gpm is a silent factor of ten.
    if (u == "dam" || u == "decameter" || u == "dm" || u == "gpdm") return "dam";
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

    // Fallback: guess Pa vs hPa from magnitude. The guess is defensible in a way
    // the geopotential one is not — no realistic pressure sample is ambiguous
    // between Pa and hPa at 2000, whereas 5000 is plausible in both gpm and
    // m2/s2, which is why toGeopotentialMeters below refuses to guess at all.
    // Still a guess, though, so it announces itself rather than silently deciding
    // what a pressure axis means.
    const bool assumePa = value > 2000.0;

    // Once per (unit, assumption), not once per call: a cross-section runs this
    // per path point per level, and a warning per sample would bury the message
    // it is trying to deliver.
    static std::mutex warnedMutex;
    static std::set<std::pair<std::string, bool>> warned;
    {
        std::lock_guard<std::mutex> lock(warnedMutex);
        if (warned.emplace(units, assumePa).second)
            logf(LogLevel::Warn, "toHpa: unrecognized pressure unit '{}'; assuming {}", units,
                 assumePa ? "Pa" : "hPa");
    }
    return assumePa ? value * 1e-2 : value;
}

double toGeopotentialMeters(double value, const std::string& units) {
    if (const auto c = convert(value, units, "gpm")) return *c;
    // Geometric metres are not geopotential metres, but the difference is under
    // 0.5% below 30 km — irrelevant next to a height label on a log-p axis, and
    // NetCDF geopotential-height fields routinely ship as plain "m".
    if (canon(units) == "m") return value;
    return std::numeric_limits<double>::quiet_NaN();
}

std::optional<std::string> preferredDisplayUnit(const std::string& units) {
    const std::string u = canon(units);
    if (u == "K") return std::string("Cel");
    if (u == "Pa") return std::string("hPa");
    // Geopotential is almost always read as a height; show the gpm equivalent.
    if (u == "m2/s2") return std::string("gpm");
    return std::nullopt;
}

std::vector<std::string> alternativeUnits(const std::string& units) {
    // Deliberately spelled out next to convert() rather than derived from it:
    // convert() is a flat list of ordered pairs, and reversing those into families
    // at runtime would be more code than the six lines it would replace.
    static const std::vector<std::vector<std::string>> kFamilies = {
        {"K", "Cel"},      {"Pa", "hPa"}, {"m/s", "kt"}, {"gpm", "dam", "m2/s2"},
        {"kg/kg", "g/kg"}, {"m", "mm"},
    };

    const std::string u = canon(units);
    for (const std::vector<std::string>& family : kFamilies) {
        if (std::find(family.begin(), family.end(), u) == family.end()) continue;
        std::vector<std::string> out{u};  // native first, whatever order the family lists
        for (const std::string& alt : family)
            if (alt != u) out.push_back(alt);
        return out;
    }
    // canon() leaves an unrecognized unit alone, so this hands back what was asked
    // for rather than an empty list every caller would have to guard against.
    return {u};
}

namespace {

struct UnitFactor {
    std::string symbol;
    int exponent = 1;
};

bool isSymbolChar(unsigned char c) {
    // Bytes >= 0x80 admit UTF-8 symbols such as "°".
    return std::isalpha(c) || c == '_' || c == '%' || c >= 0x80;
}

// One factor: a symbol, then an optional exponent written "**-1", "^-1", "-1" or
// "2". Anything else in the token and the whole string is left alone.
std::optional<UnitFactor> parseFactor(const std::string& token) {
    std::size_t i = 0;
    while (i < token.size() && isSymbolChar(static_cast<unsigned char>(token[i]))) ++i;
    if (i == 0) return std::nullopt;
    UnitFactor f{token.substr(0, i), 1};
    std::string rest = token.substr(i);
    if (rest.rfind("**", 0) == 0) rest.erase(0, 2);
    else if (rest.rfind('^', 0) == 0) rest.erase(0, 1);
    if (rest.empty()) return token.size() == i ? std::optional<UnitFactor>(f) : std::nullopt;
    std::size_t digits = rest[0] == '-' || rest[0] == '+' ? 1 : 0;
    if (digits == rest.size()) return std::nullopt;
    for (std::size_t k = digits; k < rest.size(); ++k)
        if (!std::isdigit(static_cast<unsigned char>(rest[k]))) return std::nullopt;
    f.exponent = std::stoi(rest);
    if (f.exponent == 0) return std::nullopt;
    return f;
}

// Space-separated factors, each exponent multiplied by `sign`.
bool parseFactors(const std::string& text, int sign, std::vector<UnitFactor>& out) {
    std::size_t start = 0;
    bool any = false;
    while (start <= text.size()) {
        std::size_t end = text.find(' ', start);
        if (end == std::string::npos) end = text.size();
        if (end > start) {
            const std::optional<UnitFactor> f = parseFactor(text.substr(start, end - start));
            if (!f) return false;
            out.push_back({f->symbol, f->exponent * sign});
            any = true;
        }
        start = end + 1;
    }
    return any;
}

std::string exponentText(int exponent, bool ascii) {
    const std::string digits = std::to_string(exponent);
    if (ascii) return digits;
    // Escaped rather than typed: the build sets no source charset, and several of
    // these encode bytes code page 1252 leaves undefined.
    static const char* const kSuperscript[] = {
        "\xE2\x81\xB0", "\xC2\xB9",     "\xC2\xB2",     "\xC2\xB3",     "\xE2\x81\xB4",
        "\xE2\x81\xB5", "\xE2\x81\xB6", "\xE2\x81\xB7", "\xE2\x81\xB8", "\xE2\x81\xB9"};
    constexpr const char* kSuperscriptMinus = "\xE2\x81\xBB";
    std::string out;
    for (const char d : digits) out += d == '-' ? kSuperscriptMinus : kSuperscript[d - '0'];
    return out;
}

std::string formatUnits(const std::string& units, bool ascii) {
    if (canon(units) == "Cel")
        return ascii ? "degC"
                     : "\xC2\xB0"
                       "C";

    std::vector<UnitFactor> factors;
    const std::size_t slash = units.find('/');
    if (slash == std::string::npos) {
        if (!parseFactors(units, 1, factors)) return units;
    } else {
        if (units.find('/', slash + 1) != std::string::npos) return units;
        if (!parseFactors(units.substr(0, slash), 1, factors) ||
            !parseFactors(units.substr(slash + 1), -1, factors))
            return units;
    }

    const auto join = [ascii](const std::vector<UnitFactor>& fs) {
        std::string out;
        for (const UnitFactor& f : fs) {
            if (!out.empty()) out += ' ';
            out += f.symbol;
            if (f.exponent != 1) out += exponentText(f.exponent, ascii);
        }
        return out;
    };
    std::vector<UnitFactor> above;
    std::vector<UnitFactor> below;
    for (const UnitFactor& f : factors) {
        if (f.exponent > 0) above.push_back(f);
        else below.push_back({f.symbol, -f.exponent});
    }
    if (below.empty()) return join(above);
    const std::string numerator = above.empty() ? std::string("1") : join(above);
    const std::string denominator = join(below);
    return numerator + "/" + (below.size() > 1 ? "(" + denominator + ")" : denominator);
}

}  // namespace

std::string unitLabel(const std::string& units) { return formatUnits(units, /*ascii=*/false); }

std::string unitLabelAscii(const std::string& units) { return formatUnits(units, /*ascii=*/true); }

}  // namespace met::core
