#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

#include <QMainWindow>
#include <QPointer>
#include <QStringList>

#include "viewer/analysis/wind.h"
#include "viewer/app/fieldcache.h"
#include "viewer/app/jobs.h"
#include "viewer/core/field.h"
#include "viewer/core/geo.h"
#include "viewer/readers/ireader.h"

class QCheckBox;
class QComboBox;
class QDockWidget;
class QDoubleSpinBox;
class QLabel;
class QMenu;
class QProgressBar;
class QSlider;
class QThreadPool;
class QTimer;

namespace met::analysis {
struct Sounding;
struct CrossSection;
}  // namespace met::analysis

namespace met::app {

class DatasetDock;
class PlotView2D;
class MapView;
class TileLayer;
class ColorbarWidget;
class TimeController;
class ThemeManager;
class IconThemer;
class CrossSectionView;
class SkewTView;
class ViewFrame;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

    // The app-wide theme controller (light/dark). Exposed so main() can swap the
    // window icon variant when the effective scheme changes.
    [[nodiscard]] ThemeManager* theme() const { return theme_; }

    // Open a single file directly (File > Open Recent). Thin wrapper over
    // openFiles({path}, replace=true).
    void openFile(const QString& path);

    // Open a set of files as one merged dataset whose time axis is the union of
    // the inputs (e.g. HRRR's one-file-per-hour). `replace` starts a fresh set;
    // when false the files are added to the current set. Asynchronous: files are
    // scanned on a background thread with a progress bar so the UI stays
    // responsive (a full HRRR day is ~23 files, ~0.6 s each to scan).
    void openFiles(const QStringList& paths, bool replace);

    // Synchronous variant for headless/CLI use: the event loop isn't running yet
    // and follow-on flags (--var/--time/--grab) must act on a ready dataset.
    void openFilesBlocking(const QStringList& paths);

    // Developer/testing aid: after `delayMs`, grab the plot to `pngPath` and
    // quit the application. Used for headless visual verification.
    void scheduleGrabAndQuit(const QString& pngPath, int delayMs = 1200);

    // Programmatically toggle the contour overlay (used by tests / --contours).
    void setContoursChecked(bool on);

    // Switch the central view to the GIS map tab (used by --map).
    void showMapTab();

    // Start time-animation playback (used by --play).
    void startPlayback();

    // Force the CPU warp path (used by --cpu for comparison).
    void setGpuChecked(bool on);

    // Set the wind overlay mode (0 off, 1 barbs, 2 streamlines); used by --wind.
    void setWindComboIndex(int index);

    // Set the derived-quantity combo index; used by --derived.
    void setDerivedComboIndex(int index);

    // Select the displayed variable by canonical short name (used by --var).
    void selectVariable(const QString& name);

    // Select the pressure level in hPa on the current variable (used by --level).
    void selectLevelHpa(double hPa);

    // Set the colormap on the 2D-plot and map views by name (used by --colormap).
    void setColormapByName(const QString& name);

    // Set the map basemap by name (used by --basemap).
    void setBasemapByName(const QString& name);

    // Set the sample point the --demo triggers use (used by --demo-at).
    void setDemoPoint(double lat, double lon);

    // Jump to a time step by index (used by --time, e.g. to render GIF frames).
    void setTimeIndex(int index);

    // Headless demo triggers (used by --section / --sounding / --series).
    void demoCrossSection();
    void demoSounding();
    void demoTimeSeries();
    // Open a cross-section and a skew-T side by side to demonstrate a tiled layout
    // (used by --tile).
    void demoTiledLayout();

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onOpenTriggered();
    void onOpenFolderTriggered();
    void onAddFilesTriggered();
    void onFieldChosen(const core::FieldKey& key);
    void onLevelChanged(int index);
    void onTimeChanged(int index);
    void onDerivedChanged(int index);
    void onCrossSectionRequested(const std::vector<core::LatLon>& path);
    void onSoundingRequested(core::LatLon point);
    void onTimeSeriesRequested(core::LatLon point);
    void onProbeMoved(double lat, double lon, double value, bool hasValue);
    void onProbeLeft();

private:
    void buildUi();
    // Build the 2D-Plot / Map tabs, each a ViewFrame whose control panel owns that
    // view's display controls (colormap/range/legend + view-specific controls).
    ViewFrame* buildPlotFrame();
    ViewFrame* buildMapFrame();
    // Wrap a cross-section view in a ViewFrame with its own colormap/range/legend.
    ViewFrame* wrapCrossSection(CrossSectionView* view);
    // Add an analysis view as a closable, dockable, floatable panel in the view
    // area (drag its tab to split/tab/float). Deleted on close. Returns the dock.
    QDockWidget* addAnalysisDock(QWidget* frame, const QString& title);
    void updateShowingLabel();  // "Showing: <quantity> @ <level>, <valid time>"
    // Re-run every open analysis tab's extraction at the current time so sections/
    // soundings follow the time slider and the time-series marker tracks it.
    void refreshAnalyses();

    // One open analysis tab: its tab widget (for close cleanup), a closure that
    // re-extracts/updates it at the current time, and a predicate reporting whether
    // an extraction is currently in flight (so playback can wait for it).
    struct OpenAnalysis {
        QPointer<QWidget> frame;
        std::function<void()> refresh;
        std::function<bool()> loading;  // true while this tab is extracting
    };
    std::vector<OpenAnalysis> analyses_;

    // Per-tab state for a cross-section / skew-T that follows the time slider:
    // coalesces refreshes to one extraction in flight and caches the extracted
    // result per (validTime, member) so a looping Play recomputes nothing after the
    // first pass. Defined in the .cpp (they hold analysis result structs).
    struct CrossSectionTab;
    struct SoundingTab;
    void refreshCrossSectionTab(QPointer<CrossSectionView> view, std::string var,
                                std::vector<core::LatLon> path,
                                std::shared_ptr<CrossSectionTab> tab);
    void refreshSoundingTab(QPointer<SkewTView> view, core::LatLon point,
                            std::shared_ptr<SoundingTab> tab);

    void decodeCurrent();  // decode the field for the current var/level/time
    void displayField(std::shared_ptr<core::Field2D> field);  // show a decoded field
    void presentField();   // show the current raw field or a derived quantity
    void prefetchAhead();  // decode upcoming time steps into the cache
    // Closed-loop playback: advance to the next frame only once the current one is
    // fully loaded — the field decode AND every open analysis (cross-section/skew-T)
    // extraction. Called at each of those settle points; fires TimeController::
    // frameReady() when nothing is left outstanding.
    void maybeAdvancePlayback();
    void updateWind();     // (re)build the wind overlay for the current level/time
    // Build the earth-relative wind field for the current level/time, or null.
    std::shared_ptr<analysis::WindField> buildWindField();
    void loadSettings();
    void saveSettings();
    void openPreferences();
    void addRecentFile(const QString& path);  // record a successfully opened file
    void updateRecentMenu();                   // rebuild the "Open Recent" submenu

    // A batch of opened files: the successfully opened (path, dataset) pairs in
    // order, plus paths that failed to open (skipped, not fatal).
    struct OpenBatch {
        std::vector<std::pair<std::filesystem::path, std::shared_ptr<readers::IDataset>>> opened;
        QStringList skipped;
    };
    // Open each path, catching per-file failures; bumps progress->done per file if
    // given. Static + touches no window state so it can run on a worker thread.
    static OpenBatch openBatch(const std::vector<std::filesystem::path>& paths,
                               JobProgress* progress);
    // Install an opened batch on the GUI thread: merge it into the current set (or
    // replace the set), rebuild the catalog view, and show or preserve the field.
    void installBatch(OpenBatch batch, bool replace);
    // The paths a request should actually open: for an add, drops any already in
    // the current set and de-dupes; sorted for deterministic order.
    std::vector<std::filesystem::path> pathsToOpen(const QStringList& paths, bool replace) const;

    // Read all pressure levels of `varName` at `time` (pressure, field). Static +
    // dataset-by-reference so it can run off the GUI thread without touching
    // mutable window state; readField is serialized inside the reader.
    // The read/compute helpers take an optional `onRead` callback invoked once per
    // decoded slab (from the worker thread) so a background job can report progress.
    static std::vector<std::pair<double, core::Field2D>> readLevelStack(
        readers::IDataset& ds, const std::string& varName, core::TimePoint time, int member,
        const std::function<void()>& onRead = {});
    // Read all native model levels (hybrid/sigma) of `varName` at `time`, keyed by
    // the model-level index (not pressure); pair with the `pres` field to place them.
    static std::vector<std::pair<double, core::Field2D>> readModelLevelStack(
        readers::IDataset& ds, const std::string& varName, core::TimePoint time, int member,
        const std::function<void()>& onRead = {});
    // Read all times of `varName` at `level` (time, field).
    static std::vector<std::pair<core::TimePoint, core::Field2D>> readTimeStack(
        readers::IDataset& ds, const std::string& varName, core::VerticalLevel level, int member,
        const std::function<void()>& onRead = {});
    // Read the U/V wind stacks at `time` into uStack/vStack (both left empty when the
    // dataset has no recognizable wind pair). `modelLevels` reads native model levels
    // instead of pressure levels.
    static void readWindStacks(readers::IDataset& ds, core::TimePoint time, int member,
                               std::vector<std::pair<double, core::Field2D>>& uStack,
                               std::vector<std::pair<double, core::Field2D>>& vStack,
                               bool modelLevels = false, const std::function<void()>& onRead = {});
    // Extract a sounding / cross-section, choosing the pressure-level path when the
    // variable has isobaric levels, else the native model-level path (via `pres`).
    // When `progress` is set, each slab read bumps its counter, and `generating` is
    // flipped once loading finishes and the (unmeasured) extraction begins.
    static analysis::Sounding computeSounding(readers::IDataset& ds, core::TimePoint time,
                                              int member, core::LatLon point,
                                              std::shared_ptr<JobProgress> progress = {});
    static analysis::CrossSection computeCrossSection(readers::IDataset& ds,
                                                      const std::string& var, core::TimePoint time,
                                                      int member,
                                                      const std::vector<core::LatLon>& path,
                                                      int nSamples,
                                                      std::shared_ptr<JobProgress> progress = {});

    // Number of slab reads a compute* will perform (catalog-only, no I/O), used to
    // size the progress bar before the job starts.
    static int estimateSoundingReads(readers::IDataset& ds);
    static int estimateCrossSectionReads(readers::IDataset& ds, const std::string& var);

    // Background-job progress: show/hide a shared status-bar bar as jobs come and go.
    void beginJob(const QString& text, std::shared_ptr<JobProgress> progress);
    void endJob(const std::shared_ptr<JobProgress>& progress);
    void pollProgress();

    std::shared_ptr<readers::IDataset> dataset_;
    // The currently loaded set: the leaf datasets (kept so "Add files" only scans
    // the new files) and their paths, parallel and in load order. `dataset_` is the
    // sole leaf when one file is loaded, else a MultiDataset wrapping all of them.
    std::vector<std::filesystem::path> loadedPaths_;
    std::vector<std::shared_ptr<readers::IDataset>> loadedSources_;
    quint64 openGeneration_ = 0;  // supersede an in-flight async open when a newer one starts
    QString currentUnits_;
    quint64 generation_ = 0;
    quint64 datasetEpoch_ = 0;  // bumped on dataset *replace*; invalidates per-tab analysis caches
    bool fieldReadyForStep_ = false;  // current frame's field has settled (for playback gating)
    FieldCache fieldCache_{1024ull * 1024 * 1024};  // 1 GB default
    int cacheBudgetMB_ = 1024;
    int animationFps_ = 6;
    int prefetchAhead_ = 4;

    // Current selection state.
    std::string currentVar_;
    std::vector<core::VerticalLevel> currentLevels_;
    std::vector<core::TimePoint> currentTimes_;
    int currentMember_ = -1;
    int levelIdx_ = 0;
    int timeIdx_ = 0;
    std::shared_ptr<core::Field2D> currentRaw_;  // last decoded raw field
    core::LatLon demoPoint_{64.0, 12.0};         // sample point for the --demo triggers
    int derivedMode_ = 0;                        // 0 = none; see the Derived combo
    bool showingDerived_ = false;                // a derived quantity is actually on screen
    int analysisSeq_ = 0;                        // unique object-name counter for analysis docks
    // --tile: the analysis docks are created asynchronously, so collect the next
    // `tilePending_` of them and, once the second arrives, split them side by side.
    int tilePending_ = 0;
    QDockWidget* tileFirst_ = nullptr;

    // Center is a nested QMainWindow whose dock widgets are the views; users drag a
    // view's tab/title to split the area, tab views together, or float them out.
    QMainWindow* viewArea_ = nullptr;
    QDockWidget* plotDock_ = nullptr;  // base view docks (non-closable)
    QDockWidget* mapDock_ = nullptr;
    DatasetDock* datasetDock_ = nullptr;
    PlotView2D* plot_ = nullptr;
    MapView* mapView_ = nullptr;
    TileLayer* tileLayer_ = nullptr;
    ViewFrame* plotFrame_ = nullptr;  // view wrappers (canvas + control panel)
    ViewFrame* mapFrame_ = nullptr;

    // Global data selection (left "Data" dock).
    QComboBox* levelCombo_ = nullptr;
    QComboBox* derivedCombo_ = nullptr;
    QLabel* showingLabel_ = nullptr;

    // Per-view display controls, kept for persistence / test hooks / derived sync.
    QComboBox* plotColormapCombo_ = nullptr;
    QComboBox* mapColormapCombo_ = nullptr;
    QCheckBox* plotContourCheck_ = nullptr;
    QComboBox* mapBasemapCombo_ = nullptr;
    QSlider* mapOpacitySlider_ = nullptr;
    QCheckBox* mapViewRangeCheck_ = nullptr;
    QCheckBox* mapGraticuleCheck_ = nullptr;
    QCheckBox* mapCoastlineCheck_ = nullptr;
    QCheckBox* mapContourCheck_ = nullptr;
    QCheckBox* mapGpuCheck_ = nullptr;
    QComboBox* plotWindCombo_ = nullptr;
    QComboBox* mapWindCombo_ = nullptr;

    TimeController* timeController_ = nullptr;
    ThemeManager* theme_ = nullptr;
    IconThemer* icons_ = nullptr;
    QLabel* probeLabel_ = nullptr;
    QMenu* recentMenu_ = nullptr;  // File > Open Recent
    QThreadPool* pool_ = nullptr;

    // Background-job progress bar (status bar) and the jobs currently feeding it.
    QProgressBar* progressBar_ = nullptr;
    QTimer* progressTimer_ = nullptr;
    std::vector<std::shared_ptr<JobProgress>> activeJobs_;
};

}  // namespace met::app
