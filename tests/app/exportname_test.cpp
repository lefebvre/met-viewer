#include <gtest/gtest.h>

#include <QString>

#include "viewer/analysis/crosssection.h"
#include "viewer/analysis/sounding.h"
#include "viewer/analysis/timeseries.h"
#include "viewer/app/exportname.h"
#include "viewer/core/timeaxis.h"

using namespace met;
using app::latLonTag;
using app::levelTag;
using Type = core::VerticalLevel::Type;

namespace {
const core::TimePoint kNoon{core::timegmUtc(2024, 5, 1, 12, 0, 0)};

analysis::Sounding sounding(core::LatLon at, core::TimePoint t, int member) {
    analysis::Sounding s;
    s.point = at;
    s.validTime = t;
    s.member = member;
    s.levels.push_back({500.0, 250.0f, 240.0f});
    s.levels.push_back({850.0, 280.0f, 270.0f});
    return s;
}
}  // namespace

TEST(ExportName, SpellsAPointWithHemispheresRatherThanSigns) {
    EXPECT_EQ(latLonTag({-33.5, -70.1234}), "33.50S_70.12W");
    EXPECT_EQ(latLonTag({63.0, 10.0}), "63.00N_10.00E");
}

TEST(ExportName, SpellsEachKindOfLevelWithoutSpaces) {
    EXPECT_EQ(levelTag({Type::PressureHPa, 500.0}), "500hPa");
    EXPECT_EQ(levelTag({Type::PressureHPa, 0.5}), "0.5hPa");
    EXPECT_EQ(levelTag({Type::HeightM, 2.0}), "2m");
    EXPECT_EQ(levelTag({Type::ModelLevel, 12.0}), "ml12");
    EXPECT_EQ(levelTag({Type::Sigma, 0.995}), "sigma0.995");
    EXPECT_EQ(levelTag({Type::Hybrid, 30.0}), "hybrid30");
    EXPECT_EQ(levelTag({Type::Isentropic, 310.0}), "310K");
    EXPECT_EQ(levelTag({Type::Surface, 0.0}), "surface");
    EXPECT_EQ(levelTag({Type::Unknown, 4.0}), "level4");
}

// A variable name comes from the file, and nothing obliges a file to keep "/" or
// ":" out of one.
TEST(ExportName, ReplacesWhatAFileNameCannotCarry) {
    EXPECT_EQ(app::fileSafe("TMP:2 m/above"), "TMP-2-m-above");
    EXPECT_EQ(app::fileSafe("10u_ens.mean-1"), "10u_ens.mean-1");
}

TEST(ExportName, NamesAPlotByItsFieldsVariableLevelAndTime) {
    core::Field2D f;
    f.meta.varName = "t";
    f.meta.level = {Type::PressureHPa, 850.0};
    f.meta.validTime = kNoon;
    EXPECT_EQ(app::plotFigureStem(&f), "plot_t_850hPa_20240501T1200Z");

    f.meta.member = 4;
    EXPECT_EQ(app::plotFigureStem(&f), "plot_t_850hPa_20240501T1200Z_m4");
}

TEST(ExportName, NamesASoundingByItsPointAndItsOwnTime) {
    EXPECT_EQ(app::soundingFigureStem(sounding({-33.5, -70.1234}, kNoon, -1)),
              "skewt_33.50S_70.12W_20240501T1200Z");
    EXPECT_EQ(app::soundingFigureStem(sounding({63.0, 10.0}, kNoon, 0)),
              "skewt_63.00N_10.00E_20240501T1200Z_m0");
}

// Reanalysis reaches back before 1970 (ERA5 to 1940), and midnight on the epoch is
// a time like any other: neither may be taken for "no time".
TEST(ExportName, NamesASoundingWhoseTimeIsAtOrBeforeTheUnixEpoch) {
    const core::TimePoint t1948{core::timegmUtc(1948, 1, 1, 6, 0, 0)};
    EXPECT_EQ(app::soundingFigureStem(sounding({63.0, 10.0}, t1948, -1)),
              "skewt_63.00N_10.00E_19480101T0600Z");
    EXPECT_EQ(app::soundingFigureStem(sounding({63.0, 10.0}, core::TimePoint{0}, -1)),
              "skewt_63.00N_10.00E_19700101T0000Z");
}

TEST(ExportName, NamesASectionByItsVariableBothEndsAndTime) {
    analysis::CrossSection cs;
    cs.varName = "gh";
    cs.validTime = kNoon;
    cs.points = {{40.0, -100.0}, {36.0, -92.0}, {32.0, -84.0}};
    EXPECT_EQ(app::sectionFigureStem(cs),
              "section_gh_40.00N_100.00W_to_32.00N_84.00W_20240501T1200Z");
}

TEST(ExportName, NamesASeriesByItsVariableLevelAndPointButNoTime) {
    analysis::TimeSeries ts;
    ts.varName = "t";
    ts.level = {Type::HeightM, 2.0};
    ts.point = {45.0, 10.0};
    ts.times = {core::TimePoint{0}, core::TimePoint{3600}};
    ts.values = {280.0f, 281.0f};
    EXPECT_EQ(app::seriesFigureStem(ts), "series_t_2m_45.00N_10.00E");
}

// Nothing extracted yet: no point, time or variable to go on, so just the kind.
TEST(ExportName, FallsBackToTheKindWhenAViewHasNothingToDraw) {
    EXPECT_EQ(app::plotFigureStem(nullptr), "plot");
    EXPECT_EQ(app::soundingFigureStem({}), "skewt");
    EXPECT_EQ(app::sectionFigureStem({}), "section");
    EXPECT_EQ(app::seriesFigureStem({}), "series");
}
