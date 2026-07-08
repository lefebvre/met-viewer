#pragma once

#include <functional>
#include <memory>
#include <vector>

#include <QMainWindow>
#include <QPointer>

#include "viewer/analysis/wind.h"
#include "viewer/app/fieldcache.h"
#include "viewer/core/field.h"
#include "viewer/core/geo.h"
#include "viewer/readers/ireader.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QSlider;
class QTabWidget;
class QThreadPool;

namespace met::app {

class DatasetDock;
class PlotView2D;
class MapView;
class TileLayer;
class ColorbarWidget;
class TimeController;
class CrossSectionView;
class ViewFrame;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

    // Open a file directly (used by the File menu and by command-line args).
    void openFile(const QString& path);

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

    // Headless demo triggers (used by --section / --sounding / --series).
    void demoCrossSection();
    void demoSounding();
    void demoTimeSeries();

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onOpenTriggered();
    void onFieldChosen(const core::FieldKey& key);
    void onLevelChanged(int index);
    void onTimeChanged(int index);
    void onDerivedChanged(int index);
    void onCrossSectionRequested(const std::vector<core::LatLon>& path);
    void onSoundingRequested(core::LatLon point);
    void onTimeSeriesRequested(core::LatLon point);
    void onTabCloseRequested(int index);  // close an analysis tab (not the base tabs)
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
    void updateShowingLabel();  // "Showing: <quantity> @ <level>, <valid time>"
    // Re-run every open analysis tab's extraction at the current time so sections/
    // soundings follow the time slider and the time-series marker tracks it.
    void refreshAnalyses();

    // One open analysis tab: its tab widget (for close cleanup) and a closure that
    // re-extracts/updates it at the current time.
    struct OpenAnalysis {
        QPointer<QWidget> frame;
        std::function<void()> refresh;
    };
    std::vector<OpenAnalysis> analyses_;
    void decodeCurrent();  // decode the field for the current var/level/time
    void displayField(std::shared_ptr<core::Field2D> field);  // show a decoded field
    void presentField();   // show the current raw field or a derived quantity
    void prefetchAhead();  // decode upcoming time steps into the cache
    void updateWind();     // (re)build the wind overlay for the current level/time
    // Build the earth-relative wind field for the current level/time, or null.
    std::shared_ptr<analysis::WindField> buildWindField();
    void loadSettings();
    void saveSettings();
    void openPreferences();

    // Read all pressure levels of `varName` at `time` (pressure, field). Static +
    // dataset-by-reference so it can run off the GUI thread without touching
    // mutable window state; readField is serialized inside the reader.
    static std::vector<std::pair<double, core::Field2D>> readLevelStack(
        readers::IDataset& ds, const std::string& varName, core::TimePoint time, int member);
    // Read all times of `varName` at `level` (time, field).
    static std::vector<std::pair<core::TimePoint, core::Field2D>> readTimeStack(
        readers::IDataset& ds, const std::string& varName, core::VerticalLevel level, int member);

    std::shared_ptr<readers::IDataset> dataset_;
    QString currentUnits_;
    quint64 generation_ = 0;
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
    int derivedMode_ = 0;                        // 0 = none; see the Derived combo
    bool showingDerived_ = false;                // a derived quantity is actually on screen

    QTabWidget* tabs_ = nullptr;
    DatasetDock* datasetDock_ = nullptr;
    PlotView2D* plot_ = nullptr;
    MapView* mapView_ = nullptr;
    TileLayer* tileLayer_ = nullptr;
    ViewFrame* plotFrame_ = nullptr;  // base tab wrappers (canvas + control panel)
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
    QCheckBox* mapGraticuleCheck_ = nullptr;
    QCheckBox* mapCoastlineCheck_ = nullptr;
    QCheckBox* mapGpuCheck_ = nullptr;
    QComboBox* plotWindCombo_ = nullptr;
    QComboBox* mapWindCombo_ = nullptr;

    TimeController* timeController_ = nullptr;
    QLabel* probeLabel_ = nullptr;
    QThreadPool* pool_ = nullptr;
};

}  // namespace met::app
