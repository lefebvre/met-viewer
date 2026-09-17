#include "viewer/analysis/profilewriter.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <utility>

#include <fmt/format.h>

#include "viewer/core/timeaxis.h"
#include "viewer/core/units.h"

namespace met::analysis {
namespace {

// Decimals per unit. The default is deliberately not a fixed 2: an unrecognized
// quantity can be small (a vorticity around 1e-5) and rounding it to 0.00 would
// export a measurement as a zero.
//
// Keyed on the canonical spelling, which alternativeUnits() reports as its first
// entry, so a file saying "kelvin" and one saying "K" get the same precision.
[[nodiscard]] std::optional<int> decimalsFor(const std::string& units) {
    static const std::vector<std::pair<std::string, int>> kDecimals = {
        {"hPa", 2}, {"Pa", 2},  {"gpm", 1}, {"dam", 1},  {"m", 1}, {"mm", 2},  {"K", 2},
        {"Cel", 2}, {"m/s", 2}, {"kt", 2},  {"g/kg", 3}, {"%", 1}, {"deg", 1},
    };
    const std::vector<std::string> canon = core::alternativeUnits(units);
    const std::string& u = canon.empty() ? units : canon.front();
    for (const auto& [unit, decimals] : kDecimals)
        if (u == unit) return decimals;
    return std::nullopt;
}

[[nodiscard]] std::string levelTypeLabel(core::VerticalLevel::Type t) {
    switch (t) {
        case core::VerticalLevel::Type::PressureHPa:
            return "isobaric";
        case core::VerticalLevel::Type::Hybrid:
            return "hybrid model levels";
        case core::VerticalLevel::Type::Sigma:
            return "sigma model levels";
        case core::VerticalLevel::Type::ModelLevel:
            return "model levels";
        case core::VerticalLevel::Type::HeightM:
            return "height levels";
        case core::VerticalLevel::Type::Isentropic:
            return "isentropic levels";
        case core::VerticalLevel::Type::Surface:
            return "surface";
        case core::VerticalLevel::Type::Unknown:
            break;
    }
    return "unknown";
}

// The unit a column is written in: the caller's choice, or the column's own.
[[nodiscard]] const std::string& chosenUnit(const std::string& want, const std::string& native) {
    return want.empty() ? native : want;
}

[[nodiscard]] std::string convertOut(double value, const std::string& from, const std::string& to) {
    if (std::isnan(value)) return {};
    if (from == to || to.empty()) return formatExportValue(value, from);
    // A choice the picker offered always converts; if one ever does not, writing
    // the native number is better than writing nothing, so this falls through
    // rather than blanking a real measurement.
    const std::optional<double> c = core::convert(value, from, to);
    return c ? formatExportValue(*c, to) : formatExportValue(value, from);
}

[[nodiscard]] std::string headerName(const std::string& name, const std::string& units) {
    return units.empty() ? name : fmt::format("{} ({})", name, core::unitLabelAscii(units));
}

ProfileRowOrder effectiveOrder(const PointProfile& p, const ProfileRowOrder& order) {
    if (!order.empty()) return order;
    ProfileRowOrder all(p.levels.size());
    for (std::size_t i = 0; i < all.size(); ++i) all[i] = i;
    return all;
}

// One table, shared by both writers so the CSV and the clipboard can never carry
// different numbers for the same profile.
[[nodiscard]] std::string writeTable(const PointProfile& p, const ProfileUnits& units,
                                     const ProfileRowOrder& order, char sep) {
    const bool withHeight = !p.heightSourceVar.empty();
    const std::string pressureUnit = chosenUnit(units.pressure, "hPa");
    const std::string heightUnit = chosenUnit(units.height, "gpm");

    std::vector<std::string> header;
    header.push_back("level");
    header.push_back(headerName("pressure", pressureUnit));
    if (withHeight) header.push_back(headerName("height MSL", heightUnit));
    for (std::size_t c = 0; c < p.columns.size(); ++c) {
        const std::string& want = c < units.columns.size() ? units.columns[c] : std::string();
        header.push_back(headerName(p.columns[c].id, chosenUnit(want, p.columns[c].units)));
    }

    std::string out;
    for (std::size_t i = 0; i < header.size(); ++i) {
        if (i) out += sep;
        out += csvQuote(header[i], sep);
    }
    out += '\n';

    for (const std::size_t idx : effectiveOrder(p, order)) {
        if (idx >= p.levels.size()) continue;
        const ProfileLevel& row = p.levels[idx];
        std::vector<std::string> fields;
        fields.push_back(core::formatLevel(row.level));
        fields.push_back(convertOut(row.pressureHpa, "hPa", pressureUnit));
        if (withHeight)
            fields.push_back(convertOut(static_cast<double>(row.heightGpm), "gpm", heightUnit));
        for (std::size_t c = 0; c < p.columns.size(); ++c) {
            const std::string& want = c < units.columns.size() ? units.columns[c] : std::string();
            const double v = c < row.values.size() ? static_cast<double>(row.values[c])
                                                   : std::numeric_limits<double>::quiet_NaN();
            fields.push_back(
                convertOut(v, p.columns[c].units, chosenUnit(want, p.columns[c].units)));
        }
        for (std::size_t i = 0; i < fields.size(); ++i) {
            if (i) out += sep;
            out += csvQuote(fields[i], sep);
        }
        out += '\n';
    }
    return out;
}

}  // namespace

std::string csvQuote(const std::string& field, char separator) {
    // Only the three characters that would actually break a parse. Quoting on a
    // plain space too would be safe for CSV and wrong for the clipboard: Excel
    // shows the quotes literally on a tab-separated paste, so every level label
    // would arrive with the quotes inside the cell.
    const bool needs = field.find(separator) != std::string::npos ||
                       field.find('"') != std::string::npos ||
                       field.find('\n') != std::string::npos;
    if (!needs) return field;
    std::string out = "\"";
    for (const char ch : field) {
        if (ch == '"') out += '"';  // an embedded quote is doubled
        out += ch;
    }
    out += '"';
    return out;
}

std::string formatExportValue(double value, const std::string& units) {
    if (std::isnan(value) || std::isinf(value)) return {};
    std::string out;
    if (const std::optional<int> d = decimalsFor(units)) out = fmt::format("{:.{}f}", value, *d);
    else out = fmt::format("{:.6g}", value);

    // Drop a sign that survived rounding to zero. A float that holds 273.15 is a
    // few microkelvin under it, so the Celsius conversion lands just below zero
    // and prints as "-0.00" -- which reads as a measurement below freezing rather
    // than as the zero it is.
    if (!out.empty() && out.front() == '-' && out.find_first_of("123456789") == std::string::npos)
        out.erase(out.begin());
    return out;
}

std::string profileToTsv(const PointProfile& p, const ProfileUnits& units,
                         const ProfileRowOrder& order) {
    return writeTable(p, units, order, '\t');
}

std::string profileToCsv(const PointProfile& p, const ProfileExportInfo& info,
                         const ProfileUnits& units, const ProfileRowOrder& order) {
    std::string out = "# met-viewer point profile\n";
    out += fmt::format("# point: {:.4f}, {:.4f}\n", p.point.lat, p.point.lon);
    // Extended ISO-8601, deliberately not the compact spelling the export's file
    // name uses. That form exists only because a Windows file name cannot carry a
    // colon, and the file's contents are under no such constraint -- letting a
    // filesystem limitation set a data format is the wrong way round.
    out += fmt::format("# valid time: {}\n", core::formatTime(p.validTime));
    if (!info.datasetLabel.empty()) out += fmt::format("# dataset: {}\n", info.datasetLabel);
    out +=
        fmt::format("# level type: {}\n", info.levelTypeLabel.empty() ? levelTypeLabel(p.levelType)
                                                                      : info.levelTypeLabel);
    out += fmt::format("# member: {}\n",
                       p.member < 0 ? std::string("deterministic") : fmt::format("{}", p.member));
    // Say where the altitude came from. It is mean-sea-level geopotential height
    // read from the file, never a height above ground: nothing here infers a
    // terrain elevation, so calling it AGL would be a different number entirely.
    if (p.heightSourceVar.empty()) out += "# height: none (this dataset carries no height field)\n";
    else out += fmt::format("# height: MSL geopotential height from '{}'\n", p.heightSourceVar);
    // A reader checking our direction against the file's raw u/v on a projected
    // grid would otherwise conclude we are wrong by the meridian convergence.
    if (info.windEarthRelative)
        out += "# wind: earth-relative (rotated from grid-relative where applicable)\n";
    if (!p.pointInDomain) out += "# note: this point lies outside the data domain\n";
    out += "# an empty field means no value\n";
    return out + writeTable(p, units, order, ',');
}

}  // namespace met::analysis
