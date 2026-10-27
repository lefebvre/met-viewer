#include <gtest/gtest.h>

#include <clocale>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <span>
#include <string>
#include <variant>

#include "tests/readers/arl_builder.h"
#include "viewer/core/geo.h"
#include "viewer/core/grid.h"
#include "viewer/readers/arl/arldataset.h"
#include "viewer/readers/arl/arlreader.h"
#include "viewer/readers/detect.h"

using namespace met;

namespace {
std::filesystem::path fixture(const char* n) { return std::filesystem::path(MET_FIXTURE_DIR) / n; }
}  // namespace

TEST(Arl, ProbeAcceptsAlphaGridId) {
    // Real HRRR ARL files carry a 2-char alphanumeric grid ID (e.g. "AA") in the
    // label's grid field (bytes 12-13), not digits. The probe must accept it.
    readers::arl::ArlReader reader;
    const char* label = "26 714 0 1 0AAINDX";  // 18 bytes: time/level fields, grid "AA", INDX
    const auto* bytes = reinterpret_cast<const std::byte*>(label);
    EXPECT_GT(reader.probe(std::span<const std::byte>(bytes, std::strlen(label)), "x_hrrr"), 0);
}

TEST(Arl, DecodesExtendedGridDimensions) {
    // Grid IDs below 64 (digits, '@') add nothing; the I3 header value stands.
    EXPECT_EQ(readers::arl::decodeGridDim(59, static_cast<unsigned char>('0')), 59);
    EXPECT_EQ(readers::arl::decodeGridDim(100, static_cast<unsigned char>('@')), 100);
    // HRRR: grid "AA" ('A' == 65) lifts 799 -> 1799 and 59 -> 1059.
    EXPECT_EQ(readers::arl::decodeGridDim(799, static_cast<unsigned char>('A')), 1799);
    EXPECT_EQ(readers::arl::decodeGridDim(59, static_cast<unsigned char>('A')), 1059);
    // Each step above 'A' adds another 1000 (e.g. 'C' -> +3000).
    EXPECT_EQ(readers::arl::decodeGridDim(200, static_cast<unsigned char>('C')), 3200);
}

TEST(Arl, DetectsAndCatalogs) {
    auto ds = readers::openDataset(fixture("small_latlon.arl"));
    EXPECT_EQ(ds->formatName(), "ARL");
    // Surface T02M -> t2m, and 1000 hPa TEMP -> t.
    EXPECT_NE(ds->catalog().find("t2m"), nullptr);
    const auto* t = ds->catalog().find("t");
    ASSERT_NE(t, nullptr);
    ASSERT_EQ(t->levels.size(), 1u);
    EXPECT_EQ(t->levels.front().type, core::VerticalLevel::Type::PressureHPa);
    EXPECT_DOUBLE_EQ(t->levels.front().value, 1000.0);
}

TEST(Arl, UnpacksKnownAnalyticField) {
    auto ds = readers::openDataset(fixture("small_latlon.arl"));
    const auto* v = ds->catalog().find("t2m");
    ASSERT_NE(v, nullptr);
    auto f = ds->readField(core::FieldKey{"t2m", v->levels.front(), v->times.front(), -1});
    ASSERT_EQ(f.width(), 20);
    ASSERT_EQ(f.height(), 10);
    // value(i,j) = 273 + 0.5*i + 0.3*j, recovered within the 1-byte packing
    // precision (scale 2^(7-3) = 16 -> ~0.03).
    EXPECT_NEAR(f.at(0, 0), 273.0f, 1e-3);  // == VAR1, stored exactly
    EXPECT_NEAR(f.at(19, 0), 282.5f, 0.05f);
    EXPECT_NEAR(f.at(0, 9), 275.7f, 0.05f);
    EXPECT_NEAR(f.at(19, 9), 285.2f, 0.05f);
}

TEST(Arl, LatLonGridGeometry) {
    auto ds = readers::openDataset(fixture("small_latlon.arl"));
    const auto* v = ds->catalog().find("t2m");
    auto f = ds->readField(core::FieldKey{"t2m", v->levels.front(), v->times.front(), -1});
    // size_km == 0 in the header -> regular lat/lon grid; sync (1,1) at (30,250).
    ASSERT_TRUE(std::holds_alternative<core::RegularLatLonGrid>(f.grid));
    const core::LatLon sw = core::indexToLatLon(f.grid, 0, 0);
    EXPECT_DOUBLE_EQ(sw.lat, 30.0);
    EXPECT_DOUBLE_EQ(sw.lon, 250.0);
    const core::LatLon ne = core::indexToLatLon(f.grid, 19, 9);
    EXPECT_DOUBLE_EQ(ne.lat, 48.0);
    EXPECT_DOUBLE_EQ(ne.lon, 288.0);
}

// QApplication sets the user's locale on Unix, and stod follows LC_NUMERIC: under
// a comma-decimal locale "1000.00" stopped at the '.', and every ARL file was
// refused. The reader parses with from_chars, which ignores the locale.
TEST(Arl, OpensUnderACommaDecimalLocale) {
    const std::string previous = std::setlocale(LC_NUMERIC, nullptr);
    const char* german = nullptr;
    for (const char* name : {"de_DE.UTF-8", "de_DE.utf8", "de_DE", "de-DE", "German"})
        if (std::setlocale(LC_NUMERIC, name)) {
            german = name;
            break;
        }
    if (!german) GTEST_SKIP() << "no German locale installed to test against";
    struct Restore {
        std::string locale;
        ~Restore() { std::setlocale(LC_NUMERIC, locale.c_str()); }
    } restore{previous};
    ASSERT_EQ(std::strtod("1,5", nullptr), 1.5) << german << " is not a comma-decimal locale";

    auto ds = readers::openDataset(fixture("small_latlon.arl"));
    const auto& v = ds->catalog().variables().front();
    const auto f =
        ds->readField(core::FieldKey{v.varName, v.levels.front(), v.times.front(), v.members.front()});
    EXPECT_GT(f.width(), 0);
}

namespace {
using met::testing::ArlFile;
using met::testing::ArlPeriod;
using met::testing::ArlSpec;
}  // namespace

// The INDX header says what the vertical coordinate is (K_FLAG). Inferring it
// from the level values' magnitude read hybrid levels (an offset plus a fraction,
// tens to hundreds) as pressures in hPa.
TEST(Arl, TakesTheVerticalCoordinateFromTheHeaderFlag) {
    ArlSpec spec;
    spec.kflag = 4;  // hybrid
    spec.levels = {{0.0, {"T02M"}}, {15.5, {"TEMP"}}, {31.0, {"TEMP"}}};
    ArlPeriod per;
    per.records = {{0, "T02M", 280.0}, {1, "TEMP", 275.0}, {2, "TEMP", 270.0}};
    spec.periods = {per};
    const ArlFile file("met_arl_kflag", spec);

    auto ds = readers::openDataset(file.path());
    const auto* t = ds->catalog().find("t");
    ASSERT_NE(t, nullptr);
    ASSERT_EQ(t->levels.size(), 2u);
    for (const auto& l : t->levels) EXPECT_EQ(l.type, core::VerticalLevel::Type::Hybrid);
}

// A label carries the hour only; the minutes are in each period's INDX record.
// Skipping them gave a 15-minute file four periods per hour under one time, and
// only the last survived.
TEST(Arl, KeepsSubHourlyPeriodsApart) {
    ArlSpec spec;
    ArlPeriod a, b;
    a.minutes = 0;
    a.records = {{1, "TEMP", 270.0}};
    b.minutes = 15;
    b.records = {{1, "TEMP", 280.0}};
    spec.periods = {a, b};
    const ArlFile file("met_arl_minutes", spec);

    auto ds = readers::openDataset(file.path());
    const auto* t = ds->catalog().find("t");
    ASSERT_NE(t, nullptr);
    ASSERT_EQ(t->times.size(), 2u);
    EXPECT_EQ(t->times[1].epochSeconds - t->times[0].epochSeconds, 15 * 60);
    const auto later = ds->readField(core::FieldKey{"t", t->levels.front(), t->times[1], -1});
    EXPECT_NEAR(later.at(0, 0), 280.0f, 1e-3f);
}

// SIZE is the grid spacing true at REF_LAT, not at the projection's tangent
// latitude. On a polar-stereographic grid with TANG 90 and REF 60, taking SIZE
// as projection metres made every cell about 7% too small.
TEST(Arl, PolarStereographicCellsMeasureSizeAtTheReferenceLatitude) {
    ArlSpec spec;
    //          POLE_LAT POLE_LON REF_LAT REF_LON SIZE ORIENT TANG SYNC_XP SYNC_YP SYNC_LAT SYNC_LON
    spec.grid = {90.0, 0.0, 60.0, 0.0, 100.0, 0.0, 90.0, 1.0, 1.0, 60.0, 0.0, 0.0};
    ArlPeriod per;
    per.records = {{1, "TEMP", 270.0}};
    spec.periods = {per};
    const ArlFile file("met_arl_stere", spec);

    auto ds = readers::openDataset(file.path());
    const auto& v = ds->catalog().variables().front();
    const auto f = ds->readField(core::FieldKey{v.varName, v.levels.front(), v.times.front(), -1});
    const core::LatLon a = core::indexToLatLon(f.grid, 0.0, 0.0);
    const core::LatLon b = core::indexToLatLon(f.grid, 1.0, 0.0);
    EXPECT_NEAR(a.lat, 60.0, 1e-6);
    EXPECT_NEAR(core::greatCircleKm(a, b), 100.0, 0.5);
}
