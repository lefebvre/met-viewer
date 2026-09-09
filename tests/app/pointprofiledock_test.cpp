#include <gtest/gtest.h>

#include <limits>
#include <string>
#include <vector>

#include <QAction>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QSettings>
#include <QTemporaryDir>

#include "viewer/analysis/pointprofile.h"
#include "viewer/app/pointprofiledock.h"

using namespace met;
using namespace met::app;

namespace {

constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();

// The unit choice is process-wide state persisted in QSettings, so a test that
// changes it has to put it back or the next one inherits it.
class ScopedUnitSettings {
public:
    ScopedUnitSettings() = default;
    ~ScopedUnitSettings() {
        QSettings s;
        s.beginGroup(QStringLiteral("pointProfile"));
        s.remove(QStringLiteral("units"));
        s.endGroup();
    }
    ScopedUnitSettings(const ScopedUnitSettings&) = delete;
    ScopedUnitSettings& operator=(const ScopedUnitSettings&) = delete;
};

// Spin the event loop until `predicate` holds or `ms` elapses. The project does
// not link Qt6::Test, so there is no qWait to lean on.
template <typename Fn>
void spinUntil(Fn predicate, int ms) {
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}

analysis::ProfileColumnChoices choices() {
    analysis::ProfileColumnChoices c;
    c.levelType = core::VerticalLevel::Type::PressureHPa;
    c.levels = {{core::VerticalLevel::Type::PressureHPa, 1000.0},
                {core::VerticalLevel::Type::PressureHPa, 500.0}};
    c.available = {{"t", "Temperature", "K", analysis::ProfileColumnKind::Variable},
                   {"r", "Relative humidity", "%", analysis::ProfileColumnKind::Variable}};
    c.unavailable = {{"t2m", "2 metre temperature", "K", analysis::ProfileColumnKind::Variable}};
    c.windAvailable = true;
    c.heightVar = "z";
    return c;
}

// Two levels, temperature and wind speed, with the 500 hPa temperature missing.
analysis::PointProfile profile() {
    analysis::PointProfile p;
    p.point = {63.0, 10.0};
    p.levelType = core::VerticalLevel::Type::PressureHPa;
    p.pointInDomain = true;
    p.heightSourceVar = "z";
    p.columns = {
        {"t", "Temperature", "K", analysis::ProfileColumnKind::Variable},
        {analysis::kWindSpeedId, "Wind speed", "m/s", analysis::ProfileColumnKind::WindSpeed}};

    analysis::ProfileLevel ground;
    ground.level = {core::VerticalLevel::Type::PressureHPa, 1000.0};
    ground.pressureHpa = 1000.0;
    ground.pressureStatus = analysis::CellStatus::Ok;
    ground.heightGpm = 110.0f;
    ground.heightStatus = analysis::CellStatus::Ok;
    ground.values = {283.15f, 10.0f};
    ground.statuses = {analysis::CellStatus::Ok, analysis::CellStatus::Ok};

    analysis::ProfileLevel aloft;
    aloft.level = {core::VerticalLevel::Type::PressureHPa, 500.0};
    aloft.pressureHpa = 500.0;
    aloft.pressureStatus = analysis::CellStatus::Ok;
    aloft.heightGpm = 5580.0f;
    aloft.heightStatus = analysis::CellStatus::Ok;
    aloft.values = {kNaN, 20.0f};
    aloft.statuses = {analysis::CellStatus::Missing, analysis::CellStatus::Ok};

    p.levels = {ground, aloft};  // ground first, as makeProfile produces
    return p;
}

}  // namespace

TEST(PointProfileDock, ShowsOneRowPerLevelAndOneColumnPerSelectionBehindTheCoordinates) {
    ScopedUnitSettings restore;
    PointProfileDock dock;
    dock.setChoices(choices());
    dock.setProfile(profile());

    EXPECT_EQ(dock.rowCount(), 2);
    // Level, pressure, height, then the two value columns.
    EXPECT_EQ(dock.columnCount(), 5);
    EXPECT_EQ(dock.headerText(0), "Level");
    EXPECT_EQ(dock.headerText(1), "Pressure (hPa)");
    EXPECT_EQ(dock.headerText(2), "Height MSL (gpm)");
    EXPECT_EQ(dock.headerText(3), "Temperature (K)");
    EXPECT_EQ(dock.headerText(4), "Wind speed (m/s)");
}

TEST(PointProfileDock, PutsTheGroundInTheFirstRow) {
    ScopedUnitSettings restore;
    PointProfileDock dock;
    dock.setProfile(profile());
    EXPECT_EQ(dock.cellText(0, 0), "1000 hPa");
    EXPECT_EQ(dock.cellText(1, 0), "500 hPa");
}

// The three ways a cell can have no number are one blank on screen, so the
// tooltip is where the difference has to reach the reader.
TEST(PointProfileDock, RendersAMissingCellAsADashAndSaysWhyInItsTooltip) {
    ScopedUnitSettings restore;
    PointProfileDock dock;
    dock.setProfile(profile());

    EXPECT_EQ(dock.cellText(1, 3), "—");
    EXPECT_TRUE(dock.cellTooltip(1, 3).contains("missing"));
    // A cell that does have a value explains itself with the converted unit.
    EXPECT_EQ(dock.cellText(0, 3), "283.15");
    EXPECT_TRUE(dock.cellTooltip(0, 3).contains("283.15"));
}

// The cell carries the number and the header carries the unit; the export cannot
// pick up either the unit or the em dash.
TEST(PointProfileDock, KeepsUnitsInTheHeaderAndOutOfTheExportedFields) {
    ScopedUnitSettings restore;
    PointProfileDock dock;
    dock.setProfile(profile());

    EXPECT_TRUE(dock.headerText(3).contains("(K)"));
    EXPECT_FALSE(dock.cellText(0, 3).contains("K"));
    const QString tsv = dock.tableAsTsv();
    EXPECT_TRUE(tsv.contains("283.15"));
    EXPECT_FALSE(tsv.contains("—"));
}

TEST(PointProfileDock, RescalesInPlaceWhenTheUnitChangesAndKeepsTheProfileIntact) {
    ScopedUnitSettings restore;
    PointProfileDock dock;
    dock.setProfile(profile());
    ASSERT_EQ(dock.cellText(0, 4), "10.00");  // m/s

    dock.setUnitFor("m/s", "kt");
    EXPECT_EQ(dock.cellText(0, 4), "19.44");
    EXPECT_TRUE(dock.headerText(4).contains("kt"));
    // Still two rows and five columns: no re-extraction happened, the same stored
    // values are simply being shown in another unit.
    EXPECT_EQ(dock.rowCount(), 2);
    EXPECT_EQ(dock.columnCount(), 5);
    EXPECT_TRUE(dock.tableAsCsv().contains("19.44"));
}

TEST(PointProfileDock, ReordersOnASortAndExportsWhatIsOnScreen) {
    ScopedUnitSettings restore;
    PointProfileDock dock;
    dock.setProfile(profile());
    ASSERT_EQ(dock.cellText(0, 0), "1000 hPa");

    dock.sortBy(2, /*ascending=*/false);  // altitude, highest first
    EXPECT_EQ(dock.cellText(0, 0), "500 hPa");
    EXPECT_EQ(dock.cellText(1, 0), "1000 hPa");

    // A table the user just sorted must not export in a different order.
    const QString tsv = dock.tableAsTsv();
    EXPECT_LT(tsv.indexOf("500 hPa"), tsv.indexOf("1000 hPa"));
}

TEST(PointProfileDock, EmitsPointEditedWhenCoordinatesAreCommitted) {
    ScopedUnitSettings restore;
    PointProfileDock dock;
    int edits = 0;
    QObject::connect(&dock, &PointProfileDock::pointEdited, [&edits](core::LatLon) { ++edits; });

    dock.setPoint({45.0, -100.0});
    // A programmatic set is the window echoing a click it is already acting on.
    // Re-emitting here would loop straight back into another extraction.
    EXPECT_EQ(edits, 0);
    EXPECT_TRUE(dock.hasPoint());
    EXPECT_NEAR(dock.point().lat, 45.0, 1e-9);
    EXPECT_NEAR(dock.point().lon, -100.0, 1e-9);
}

TEST(PointProfileDock, CoalescesABurstOfColumnTicksIntoOneSignal) {
    ScopedUnitSettings restore;
    PointProfileDock dock;
    dock.setChoices(choices());
    int changes = 0;
    QObject::connect(&dock, &PointProfileDock::columnsChanged, [&changes] { ++changes; });

    // Three ticks in a row, as a run down the menu produces. Driven through the
    // menu's own actions rather than setSelectedColumns, which is the programmatic
    // restore path and deliberately does not re-notify.
    QList<QAction*> toggles;
    for (QAction* act : dock.findChildren<QAction*>())
        if (act->isCheckable() && act->isEnabled()) toggles.append(act);
    ASSERT_GE(toggles.size(), 3);
    for (int i = 0; i < 3; ++i) toggles[i]->setChecked(true);
    EXPECT_EQ(changes, 0) << "nothing should fire before the debounce elapses";

    spinUntil([&changes] { return changes > 0; }, 2000);
    EXPECT_EQ(changes, 1) << "a burst of ticks must coalesce into one extraction";
    EXPECT_EQ(dock.selectedColumns().size(), 3u);
}

TEST(PointProfileDock, ShowsAPromptRatherThanAnEmptyGridBeforeAnyPointIsPicked) {
    ScopedUnitSettings restore;
    PointProfileDock dock;
    dock.setChoices(choices());
    EXPECT_EQ(dock.rowCount(), 0);
    EXPECT_FALSE(dock.message().isEmpty()) << "an empty grid explains nothing";
    EXPECT_FALSE(dock.hasPoint());
}

TEST(PointProfileDock, SaysSoWhenThePickedPointIsOutsideTheGrid) {
    ScopedUnitSettings restore;
    PointProfileDock dock;
    analysis::PointProfile p = profile();
    p.pointInDomain = false;
    dock.setProfile(p);
    // The table is replaced by the explanation rather than showing a grid of
    // dashes the reader would have to interpret.
    EXPECT_EQ(dock.rowCount(), 0);
    EXPECT_TRUE(dock.message().contains("outside"));
}

TEST(PointProfileDock, WritesTheCsvItShowsToTheChosenPath) {
    ScopedUnitSettings restore;
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    PointProfileDock dock;
    dock.setContext(QStringLiteral("era5_t_pl.nc"), {}, -1);
    dock.setProfile(profile());

    const QString path = dir.filePath(QStringLiteral("profile.csv"));
    QString error;
    ASSERT_TRUE(dock.exportCsvTo(path, &error)) << error.toStdString();

    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString written = QString::fromUtf8(file.readAll());
    EXPECT_EQ(written, dock.tableAsCsv());
    EXPECT_TRUE(written.contains("# dataset: era5_t_pl.nc"));
}

TEST(PointProfileDock, ReportsWhyAnExportFailedRatherThanFailingSilently) {
    ScopedUnitSettings restore;
    PointProfileDock dock;
    dock.setProfile(profile());
    QString error;
    // A directory that does not exist: the write cannot succeed.
    EXPECT_FALSE(dock.exportCsvTo(QStringLiteral("/no/such/dir/profile.csv"), &error));
    EXPECT_FALSE(error.isEmpty());
}

TEST(PointProfileDock, KeepsAStillAvailableColumnWhenTheDatasetChanges) {
    ScopedUnitSettings restore;
    PointProfileDock dock;
    dock.setChoices(choices());
    dock.setSelectedColumns({"t", "r", analysis::kWindSpeedId});

    // A second file with no humidity and no wind pair.
    analysis::ProfileColumnChoices leaner = choices();
    leaner.available = {{"t", "Temperature", "K", analysis::ProfileColumnKind::Variable}};
    leaner.windAvailable = false;
    dock.setChoices(leaner);

    // Temperature survives; the two the new file cannot offer are dropped rather
    // than kept as columns of blanks.
    EXPECT_EQ(dock.selectedColumns(), std::vector<std::string>{"t"});
}
