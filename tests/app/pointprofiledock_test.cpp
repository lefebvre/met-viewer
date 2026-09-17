#include <gtest/gtest.h>

#include <algorithm>
#include <limits>
#include <string>
#include <vector>

#include <QAction>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QItemSelectionModel>
#include <QSettings>
#include <QTableView>
#include <QTemporaryDir>

#include "viewer/analysis/pointprofile.h"
#include "viewer/app/pointprofiledock.h"
#include "viewer/app/pointprofilemodel.h"

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
        s.remove(QStringLiteral("columnUnits"));
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

    dock.setUnitFor(analysis::kWindSpeedId, "kt");
    EXPECT_EQ(dock.cellText(0, 4), "19.44");
    EXPECT_TRUE(dock.headerText(4).contains("kt"));
    // Still two rows and five columns: no re-extraction happened, the same stored
    // values are simply being shown in another unit.
    EXPECT_EQ(dock.rowCount(), 2);
    EXPECT_EQ(dock.columnCount(), 5);
    EXPECT_TRUE(dock.tableAsCsv().contains("19.44"));
}

// Wind components as GRIB spells their unit, a derived speed as this app spells
// the same unit, and a vertical velocity in a unit with nothing to convert to --
// the mix the Units menu has to present without merging, splitting or dropping.
analysis::PointProfile windProfile() {
    analysis::PointProfile p = profile();
    p.columns = {
        {"u", "U component of wind", "m s**-1", analysis::ProfileColumnKind::Variable},
        {analysis::kWindSpeedId, "Wind speed", "m/s", analysis::ProfileColumnKind::WindSpeed},
        {"w", "Vertical velocity", "Pa s**-1", analysis::ProfileColumnKind::Variable}};
    for (analysis::ProfileLevel& level : p.levels) {
        level.values = {10.0f, 10.0f, -0.5f};
        level.statuses.assign(3, analysis::CellStatus::Ok);
    }
    return p;
}

TEST(PointProfileDock, OffersUnitsPerColumnByTheNameItsHeaderShows) {
    ScopedUnitSettings restore;
    PointProfileDock dock;
    dock.setProfile(windProfile());

    const QStringList expected = {
        "Pressure: *hPa, Pa",   "Height MSL: *gpm, dam, m²/s²",   "U component of wind: *m/s, kt",
        "Wind speed: *m/s, kt", "Vertical velocity (Pa/s): none",
    };
    EXPECT_EQ(dock.unitMenuEntries(), expected);
}

// Two columns in the same unit are still two quantities: knots for the wind speed
// says nothing about how the reader wants the U component shown.
TEST(PointProfileDock, ConvertsOnlyTheColumnWhoseUnitWasChosen) {
    ScopedUnitSettings restore;
    PointProfileDock dock;
    dock.setProfile(windProfile());

    dock.setUnitFor(analysis::kWindSpeedId, "kt");
    EXPECT_EQ(dock.headerText(4), "Wind speed (kt)");
    EXPECT_EQ(dock.cellText(0, 4), "19.44");
    EXPECT_EQ(dock.headerText(3), "U component of wind (m/s)");
    EXPECT_TRUE(dock.unitMenuEntries().contains("Wind speed: m/s, *kt"));
    EXPECT_TRUE(dock.unitMenuEntries().contains("U component of wind: *m/s, kt"));
}

TEST(PointProfileDock, RemembersAColumnsUnitAcrossPanels) {
    ScopedUnitSettings restore;
    {
        PointProfileDock first;
        first.setProfile(profile());
        first.setUnitFor("t", "Cel");
        first.setUnitFor(PointProfileModel::kPressureKey, "Pa");
    }
    PointProfileDock second;
    second.setProfile(profile());
    EXPECT_EQ(second.headerText(1), "Pressure (Pa)");
    EXPECT_EQ(second.headerText(3), "Temperature (°C)");
    EXPECT_EQ(second.cellText(0, 3), "10.00");
}

// A choice is kept by variable id, and another file can use the same id for a
// quantity in a unit the choice cannot reach. Applying it would blank the column.
TEST(PointProfileDock, IgnoresARememberedUnitTheColumnCannotConvertTo) {
    ScopedUnitSettings restore;
    PointProfileDock dock;
    dock.setUnitFor("t", "Cel");

    analysis::PointProfile other = profile();
    other.columns[0].units = "%";
    dock.setProfile(other);
    EXPECT_EQ(dock.headerText(3), "Temperature (%)");
    EXPECT_EQ(dock.cellText(0, 3), "283.1");
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

// An empty table on the first pick would be honest and useless, so a dataset
// arriving with nothing chosen yet opens on the quantities a site readout is
// usually wanted for.
TEST(PointProfileDock, OpensOnADefaultSelectionRatherThanAnEmptyTable) {
    ScopedUnitSettings restore;
    PointProfileDock dock;
    EXPECT_TRUE(dock.selectedColumns().empty()) << "nothing to choose from before a dataset";

    dock.setChoices(choices());
    const std::vector<std::string> chosen = dock.selectedColumns();
    EXPECT_NE(std::find(chosen.begin(), chosen.end(), "t"), chosen.end());
    EXPECT_NE(std::find(chosen.begin(), chosen.end(), "r"), chosen.end());
    EXPECT_NE(std::find(chosen.begin(), chosen.end(), analysis::kWindSpeedId), chosen.end());
}

// Clearing every box is a decision, not an absence of one: the next dataset must
// not silently repopulate the table the user just emptied.
TEST(PointProfileDock, DoesNotRepopulateASelectionTheUserDeliberatelyCleared) {
    ScopedUnitSettings restore;
    PointProfileDock dock;
    dock.setChoices(choices());
    ASSERT_FALSE(dock.selectedColumns().empty());

    dock.setSelectedColumns({});
    dock.setChoices(choices());  // a second file with the same variables
    EXPECT_TRUE(dock.selectedColumns().empty());
}

TEST(PointProfileDock, CoalescesABurstOfColumnTicksIntoOneSignal) {
    ScopedUnitSettings restore;
    PointProfileDock dock;
    dock.setChoices(choices());
    // Start from nothing selected. setChoices opens on a default set, and ticking
    // a box that is already ticked is not a change, so the burst below would
    // otherwise be three no-ops.
    dock.setSelectedColumns({});
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

// A selection copy is the cells' numbers, not their on-screen text: the "—" a
// missing cell shows would sit in a spreadsheet column as a stray character,
// and the empty field is what the CSV already writes for the same cell.
TEST(PointProfileDock, CopiesTheSelectionAsBareNumbersWithoutAHeader) {
    ScopedUnitSettings restore;
    PointProfileDock dock;
    dock.setProfile(profile());

    auto* table = dock.findChild<QTableView*>();
    ASSERT_TRUE(table != nullptr);
    table->show();  // a selection happens in a visible panel
    auto* selection = table->selectionModel();
    ASSERT_TRUE(selection != nullptr);
    // Both temperature cells, in view order (the table opens ground-first).
    selection->select(table->model()->index(0, 3), QItemSelectionModel::Select);
    selection->select(table->model()->index(1, 3), QItemSelectionModel::Select);

    // The 500 hPa temperature is missing, so its field is empty, not a dash.
    EXPECT_EQ(dock.selectionAsTsv(), "283.15\n");
}

// A cell the selection skips keeps its place with an empty field, so the paste
// lands under the right column.
TEST(PointProfileDock, KeepsSkippedCellsAlignedWithEmptyFields) {
    ScopedUnitSettings restore;
    PointProfileDock dock;
    dock.setProfile(profile());

    auto* table = dock.findChild<QTableView*>();
    ASSERT_TRUE(table != nullptr);
    table->show();  // a selection happens in a visible panel
    auto* selection = table->selectionModel();
    ASSERT_TRUE(selection != nullptr);
    // The (hidden) level label and the height, skipping the pressure between.
    selection->select(table->model()->index(0, 0), QItemSelectionModel::Select);
    selection->select(table->model()->index(0, 2), QItemSelectionModel::Select);

    EXPECT_EQ(dock.selectionAsTsv(), "\t110.0");
}

// The level column is hidden on an isobaric axis, so a whole-row copy starts at
// the pressure and carries no field for it at all.
TEST(PointProfileDock, CopiesAWholeRowWithoutTheHiddenLevelColumn) {
    ScopedUnitSettings restore;
    PointProfileDock dock;
    dock.setProfile(profile());

    auto* table = dock.findChild<QTableView*>();
    ASSERT_TRUE(table != nullptr);
    table->show();  // a selection happens in a visible panel
    table->selectionModel()->select(
        QItemSelection(table->model()->index(0, 0), table->model()->index(0, 4)),
        QItemSelectionModel::Select);

    EXPECT_EQ(dock.selectionAsTsv(), "1000.00\t110.0\t283.15\t10.00");
}

TEST(PointProfileDock, FallsBackToTheWholeTableWhenNothingIsSelected) {
    ScopedUnitSettings restore;
    PointProfileDock dock;
    dock.setProfile(profile());

    auto* table = dock.findChild<QTableView*>();
    ASSERT_TRUE(table != nullptr);
    table->show();  // a selection happens in a visible panel
    table->selectionModel()->clearSelection();

    // The same text the Copy button produces, header included.
    EXPECT_EQ(dock.selectionAsTsv(), dock.tableAsTsv());
}

// The save dialog opens on a name that says where the profile is from and the
// time it was read at: the point from the panel, the valid time from the profile
// itself, with the separators a file name would not want.
TEST(PointProfileDock, SuggestsANameCarryingThePointAndTheProfilesOwnTime) {
    ScopedUnitSettings restore;
    PointProfileDock dock;
    analysis::PointProfile p = profile();
    p.validTime = {core::timegmUtc(2024, 5, 1, 12, 0, 0)};
    dock.setPoint({-33.5, -70.1234});
    dock.setProfile(p);

    EXPECT_EQ(dock.suggestedCsvName(), "profile_33.50S_70.12W_20240501T1200Z.csv");
}

TEST(PointProfileDock, SuggestsANameWithoutATimeWhenThereIsNoProfileToStamp) {
    ScopedUnitSettings restore;
    PointProfileDock dock;
    dock.setPoint({63.0, 10.0});
    dock.setProfile({});  // nothing extracted, so there is no time to name it by

    EXPECT_EQ(dock.suggestedCsvName(), "profile_63.00N_10.00E.csv");
}

// Reanalysis routinely predates 1970 -- ERA5 reaches 1940 -- so the epoch
// seconds go negative. Testing the timestamp's sign to decide whether there is
// one to print dropped the time from the name for every such file.
TEST(PointProfileDock, NamesAProfileWhoseTimeIsBeforeTheUnixEpoch) {
    ScopedUnitSettings restore;
    PointProfileDock dock;
    analysis::PointProfile p = profile();
    p.validTime = {core::timegmUtc(1948, 1, 1, 6, 0, 0)};
    dock.setPoint({63.0, 10.0});
    dock.setProfile(p);

    EXPECT_EQ(dock.suggestedCsvName(), "profile_63.00N_10.00E_19480101T0600Z.csv");
}

// Midnight on the epoch is a time like any other, not a missing one.
TEST(PointProfileDock, NamesAProfileValidAtTheEpochItself) {
    ScopedUnitSettings restore;
    PointProfileDock dock;
    analysis::PointProfile p = profile();
    p.validTime = {0};
    dock.setPoint({63.0, 10.0});
    dock.setProfile(p);

    EXPECT_EQ(dock.suggestedCsvName(), "profile_63.00N_10.00E_19700101T0000Z.csv");
}
