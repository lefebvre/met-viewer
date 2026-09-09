#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "viewer/analysis/pointprofile.h"

namespace met::analysis {

// Serializing a PointProfile to text, for a clipboard paste and for a CSV file.
//
// This lives in the analysis layer rather than beside the widget for two reasons.
// It is pure data-to-text with no Qt in it, so its tests run in the fast Qt-free
// target with no QApplication. And a machine-readable export must never be
// translated, so being in a layer with no tr() is a feature: a localized column
// header would break every script downstream of it.
//
// The display/export split is the whole point. The app's formatValueWithUnits
// renders "273.15 K (0.00 Cel)", which is right for a cell a person reads and
// wrong for a column a script parses.

// Where an exported profile came from. None of it is recoverable from the
// numbers, so a file found in a folder months later would otherwise be a grid of
// anonymous digits.
struct ProfileExportInfo {
    std::string datasetLabel;    // the file(s) as loaded
    std::string levelTypeLabel;  // "isobaric", "hybrid model levels", ...
    bool windEarthRelative = true;
};

// The unit each value should be written in. `columns` is parallel to
// PointProfile::columns; an empty entry means the column's own native unit.
// Values are stored natively and converted on the way out, so the header and the
// numbers are produced from one choice and cannot disagree.
struct ProfileUnits {
    std::vector<std::string> columns;
    std::string pressure;  // empty = hPa, the unit the coordinate is stored in
    std::string height;    // empty = gpm
};

// Which rows to write and in what order, as indices into PointProfile::levels.
// The dock passes what is on screen: a table the user has just sorted and then
// exported should not come back in a different order. Empty means every row in
// the profile's own ground-first order.
using ProfileRowOrder = std::vector<std::size_t>;

// The full CSV: '#'-prefixed provenance, then a header row carrying units, then
// the rows.
[[nodiscard]] std::string profileToCsv(const PointProfile& p, const ProfileExportInfo& info,
                                       const ProfileUnits& units = {},
                                       const ProfileRowOrder& order = {});

// Tab-separated header and rows, for the clipboard. No comment lines: a '#' line
// pasted into a spreadsheet is a junk row the reader has to delete.
[[nodiscard]] std::string profileToTsv(const PointProfile& p, const ProfileUnits& units = {},
                                       const ProfileRowOrder& order = {});

// One number as written to a file: decimals chosen from the unit, and an EMPTY
// string when the value is not finite.
//
// Empty rather than "NaN" or 0 because this is read by spreadsheets: a literal
// NaN in a column silently poisons an average over it, and a zero is a wrong
// measurement rather than an absent one. Every tool already reads an empty field
// as absent.
//
// An unrecognized unit falls back to six significant digits rather than a fixed
// two, so a vorticity of 1.4e-5 does not export as 0.00.
[[nodiscard]] std::string formatExportValue(double value, const std::string& units);

// RFC 4180 quoting: wrap in quotes and double any embedded quote when the field
// contains a separator, a quote or a newline. Real unit strings need it --
// "m**2 s**-2" carries a space, and a long_name can carry a comma that would
// otherwise shift every column after it.
[[nodiscard]] std::string csvQuote(const std::string& field, char separator);

}  // namespace met::analysis
