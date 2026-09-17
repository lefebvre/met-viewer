#include <gtest/gtest.h>

#include <limits>
#include <string>
#include <vector>

#include "viewer/analysis/pointprofile.h"
#include "viewer/analysis/profilewriter.h"

using namespace met;
using namespace met::analysis;

namespace {

constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

// The first line that is not a '#' comment: the header row.
std::string headerLine(const std::string& text) {
    std::size_t pos = 0;
    while (pos < text.size()) {
        const std::size_t end = text.find('\n', pos);
        const std::string line = text.substr(pos, end - pos);
        if (!line.empty() && line[0] != '#') return line;
        if (end == std::string::npos) break;
        pos = end + 1;
    }
    return {};
}

std::vector<std::string> dataLines(const std::string& text) {
    std::vector<std::string> out;
    bool pastHeader = false;
    std::size_t pos = 0;
    while (pos < text.size()) {
        const std::size_t end = text.find('\n', pos);
        const std::string line = text.substr(pos, end - pos);
        if (!line.empty() && line[0] != '#') {
            if (pastHeader) out.push_back(line);
            pastHeader = true;
        }
        if (end == std::string::npos) break;
        pos = end + 1;
    }
    return out;
}

// Two isobaric levels, temperature and wind speed, with a height coordinate.
PointProfile sampleProfile() {
    PointProfile p;
    p.point = {63.5, 10.4};
    p.validTime = {1714564800};  // 2024-05-01T12:00Z
    p.levelType = core::VerticalLevel::Type::PressureHPa;
    p.pointInDomain = true;
    p.heightSourceVar = "z";
    p.columns = {{"t", "Temperature", "K", ProfileColumnKind::Variable},
                 {kWindSpeedId, "Wind speed", "m/s", ProfileColumnKind::WindSpeed}};

    ProfileLevel ground;
    ground.level = {core::VerticalLevel::Type::PressureHPa, 1000.0};
    ground.pressureHpa = 1000.0;
    ground.pressureStatus = CellStatus::Ok;
    ground.heightGpm = 110.9f;
    ground.heightStatus = CellStatus::Ok;
    ground.values = {273.15f, 10.0f};
    ground.statuses = {CellStatus::Ok, CellStatus::Ok};

    ProfileLevel aloft;
    aloft.level = {core::VerticalLevel::Type::PressureHPa, 500.0};
    aloft.pressureHpa = 500.0;
    aloft.pressureStatus = CellStatus::Ok;
    aloft.heightGpm = 5580.4f;
    aloft.heightStatus = CellStatus::Ok;
    aloft.values = {kNaN, 20.0f};  // temperature missing up here
    aloft.statuses = {CellStatus::Missing, CellStatus::Ok};

    p.levels = {ground, aloft};  // ground first, as makeProfile produces
    return p;
}

}  // namespace

TEST(ProfileToCsv, BeginsWithCommentedProvenanceThenAHeaderRowCarryingUnits) {
    ProfileExportInfo info;
    info.datasetLabel = "era5_t_pl.nc";
    const std::string csv = profileToCsv(sampleProfile(), info);

    EXPECT_EQ(csv.rfind("# met-viewer point profile", 0), 0u);
    EXPECT_TRUE(contains(csv, "# point: 63.5000, 10.4000"));
    // Extended ISO-8601 in the file's contents; only its name is compacted, and
    // only because a colon is illegal in a Windows file name.
    EXPECT_TRUE(contains(csv, "# valid time: 2024-05-01T12:00Z"));
    EXPECT_FALSE(contains(csv, "20240501T1200Z"));
    EXPECT_TRUE(contains(csv, "# dataset: era5_t_pl.nc"));
    EXPECT_TRUE(contains(csv, "# level type: isobaric"));
    EXPECT_EQ(headerLine(csv), "level,pressure (hPa),height MSL (gpm),t (K),wspd (m/s)");
}

// A literal NaN silently poisons a spreadsheet average over the column, and a
// zero is a wrong measurement rather than an absent one.
TEST(ProfileToCsv, WritesAMissingValueAsAnEmptyFieldNotZeroAndNotNaN) {
    const std::string csv = profileToCsv(sampleProfile(), {});
    const std::vector<std::string> rows = dataLines(csv);
    ASSERT_EQ(rows.size(), 2u);

    // 500 hPa has no temperature: the field between the height and the wind speed
    // is empty, leaving two adjacent commas.
    EXPECT_EQ(rows[1], "500 hPa,500.00,5580.4,,20.00");
    EXPECT_FALSE(contains(csv, "nan"));
    EXPECT_FALSE(contains(csv, "NaN"));
}

// The display side renders "273.15 K (0.00 °C)", which is right for a cell a
// person reads and would be unparseable in a column a script consumes.
TEST(ProfileToCsv, WritesABareNumberWithNoParenthesisedDisplayConversion) {
    const std::string csv = profileToCsv(sampleProfile(), {});
    EXPECT_TRUE(contains(csv, "273.15"));
    // The units belong in the header row, never trailing a value: every data
    // field has to parse as a bare number.
    for (const std::string& row : dataLines(csv)) {
        EXPECT_FALSE(contains(row, "("));
        EXPECT_FALSE(contains(row, "K"));
    }
}

// 273.15 K is exactly zero Celsius, but a float holding it is a few microkelvin
// short, so the naive conversion prints "-0.00" and reads as below freezing.
TEST(ProfileToCsv, DoesNotWriteANegativeZeroForAValueThatRoundsToZero) {
    ProfileUnits units;
    units.columns = {"Cel", ""};
    const std::string csv = profileToCsv(sampleProfile(), {}, units);
    EXPECT_FALSE(contains(csv, "-0.00"));
}

TEST(ProfileToCsv, ConvertsTheValuesAndTheHeaderTogetherWhenAUnitIsChosen) {
    ProfileUnits units;
    units.columns = {"Cel", "kt"};  // temperature in Celsius, speed in knots
    const std::string csv = profileToCsv(sampleProfile(), {}, units);

    EXPECT_EQ(headerLine(csv), "level,pressure (hPa),height MSL (gpm),t (degC),wspd (kt)");
    const std::vector<std::string> rows = dataLines(csv);
    ASSERT_EQ(rows.size(), 2u);
    // 273.15 K is 0 Cel; 10 m/s is 19.44 kt.
    EXPECT_EQ(rows[0], "1000 hPa,1000.00,110.9,0.00,19.44");
}

TEST(ProfileToCsv, QuotesAFieldWhoseUnitsCarryASeparator) {
    PointProfile p = sampleProfile();
    p.columns[0].units = "W,m-2";  // a comma in the unit would shift every column
    const std::string csv = profileToCsv(p, {});
    EXPECT_TRUE(contains(headerLine(csv), "\"t (W,m-2)\""));
}

// The altitude is mean-sea-level geopotential height read from the file. Saying
// which variable it came from is what keeps it from being mistaken for a height
// above ground, which this feature deliberately does not compute.
TEST(ProfileToCsv, NamesTheVariableTheAltitudeCameFromAndSaysSoWhenThereIsNone) {
    EXPECT_TRUE(
        contains(profileToCsv(sampleProfile(), {}), "# height: MSL geopotential height from 'z'"));

    PointProfile noHeight = sampleProfile();
    noHeight.heightSourceVar.clear();
    const std::string csv = profileToCsv(noHeight, {});
    EXPECT_TRUE(contains(csv, "# height: none"));
    // And the all-blank column is left out rather than exported empty.
    EXPECT_FALSE(contains(headerLine(csv), "height MSL"));
}

// On a projected grid the file's raw u/v are grid-relative. A reader checking our
// direction against them would conclude we were wrong by the convergence angle.
TEST(ProfileToCsv, StatesThatTheWindIsEarthRelative) {
    EXPECT_TRUE(contains(profileToCsv(sampleProfile(), {}), "# wind: earth-relative"));
}

TEST(ProfileToCsv, SaysWhenThePointWasOutsideTheDataDomain) {
    PointProfile p = sampleProfile();
    p.pointInDomain = false;
    EXPECT_TRUE(contains(profileToCsv(p, {}), "outside the data domain"));
}

TEST(ProfileToCsv, WritesTheRowsInTheOrderItIsGiven) {
    // What the dock passes after the user sorts the table the other way.
    const std::string csv = profileToCsv(sampleProfile(), {}, {}, {1, 0});
    const std::vector<std::string> rows = dataLines(csv);
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_EQ(rows[0].rfind("500 hPa", 0), 0u);
    EXPECT_EQ(rows[1].rfind("1000 hPa", 0), 0u);
}

TEST(ProfileToTsv, SeparatesWithTabsAndCarriesNoCommentLines) {
    const std::string tsv = profileToTsv(sampleProfile());
    EXPECT_EQ(tsv.find('#'), std::string::npos);
    EXPECT_NE(tsv.find('\t'), std::string::npos);
    EXPECT_EQ(headerLine(tsv), "level\tpressure (hPa)\theight MSL (gpm)\tt (K)\twspd (m/s)");
}

// A level label carries a space. Quoting it would show the quotes literally in a
// spreadsheet cell on paste.
TEST(ProfileToTsv, LeavesAFieldContainingOnlySpacesUnquoted) {
    const std::string tsv = profileToTsv(sampleProfile());
    EXPECT_EQ(tsv.find('"'), std::string::npos);
    EXPECT_EQ(dataLines(tsv)[0].rfind("1000 hPa\t", 0), 0u);
}

TEST(ProfileToTsv, CarriesTheSameNumbersAsTheCsvForTheSameProfile) {
    const PointProfile p = sampleProfile();
    const std::vector<std::string> csvRows = dataLines(profileToCsv(p, {}));
    std::vector<std::string> tsvRows = dataLines(profileToTsv(p));
    ASSERT_EQ(csvRows.size(), tsvRows.size());
    for (std::size_t i = 0; i < tsvRows.size(); ++i) {
        std::string swapped = tsvRows[i];
        for (char& ch : swapped)
            if (ch == '\t') ch = ',';
        EXPECT_EQ(swapped, csvRows[i]);
    }
}

TEST(FormatExportValue, ChoosesDecimalsFromTheUnitAndAcceptsAnySpelling) {
    EXPECT_EQ(formatExportValue(273.153, "K"), "273.15");
    EXPECT_EQ(formatExportValue(273.153, "kelvin"), "273.15");  // canonicalized
    EXPECT_EQ(formatExportValue(1013.25, "hPa"), "1013.25");
    EXPECT_EQ(formatExportValue(5580.44, "gpm"), "5580.4");
    EXPECT_EQ(formatExportValue(72.46, "%"), "72.5");
}

// A fixed two decimals would export a vorticity of 1.4e-5 as "0.00" -- a real
// measurement turned into a zero with nothing to mark it as such.
TEST(FormatExportValue, FallsBackToSignificantDigitsForAnUnrecognizedUnit) {
    EXPECT_EQ(formatExportValue(1.4e-5, "s**-1"), "1.4e-05");
    EXPECT_EQ(formatExportValue(0.0, "s**-1"), "0");
}

TEST(FormatExportValue, YieldsAnEmptyStringForAValueThatIsNotFinite) {
    EXPECT_EQ(formatExportValue(std::numeric_limits<double>::quiet_NaN(), "K"), "");
    EXPECT_EQ(formatExportValue(std::numeric_limits<double>::infinity(), "K"), "");
}

TEST(CsvQuote, QuotesOnlyWhatWouldBreakAParseAndDoublesAnEmbeddedQuote) {
    EXPECT_EQ(csvQuote("plain", ','), "plain");
    EXPECT_EQ(csvQuote("with space", ','), "with space");
    EXPECT_EQ(csvQuote("a,b", ','), "\"a,b\"");
    EXPECT_EQ(csvQuote("a,b", '\t'), "a,b");  // not a separator here
    EXPECT_EQ(csvQuote("say \"hi\"", ','), "\"say \"\"hi\"\"\"");
}
