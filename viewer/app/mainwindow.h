#pragma once

#include <memory>
#include <vector>

#include <QMainWindow>

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
    void onColormapChanged(const QString& name);
    void onAutoRangeToggled(bool on);
    void onRangeSpinChanged();
    void onSymmetricToggled(bool on);
    void onContoursToggled(bool on);
    void onContourIntervalChanged(double value);
    void onBasemapChanged(int index);
    void onOpacityChanged(int percent);
    void onGraticuleToggled(bool on);
    void onCoastlinesToggled(bool on);
    void onWindModeChanged(int index);
    void onDerivedChanged(int index);
    void onCrossSectionRequested(const std::vector<core::LatLon>& path);
    void onSoundingRequested(core::LatLon point);
    void onTimeSeriesRequested(core::LatLon point);
    void onProbeMoved(double lat, double lon, double value, bool hasValue);
    void onProbeLeft();

private:
    void buildUi();
    void decodeCurrent();  // decode the field for the current var/level/time
    void displayField(std::shared_ptr<core::Field2D> field);  // show a decoded field
    void presentField();   // show the current raw field or a derived quantity
    void prefetchAhead();  // decode upcoming time steps into the cache
    void updateWind();     // (re)build the wind overlay for the current level/time
    void applyRange();     // push the current auto/manual range to the views
    void syncRangeSpins(); // populate the min/max spins from the active colormap
    // Build the earth-relative wind field for the current level/time, or null.
    std::shared_ptr<analysis::WindField> buildWindField();
    void loadSettings();
    void saveSettings();
    void openPreferences();

    // Read all pressure levels of `varName` at the current time (pressure, field).
    std::vector<std::pair<double, core::Field2D>> readLevelStack(const std::string& varName);
    // Read all times of `varName` at the current level (time, field).
    std::vector<std::pair<core::TimePoint, core::Field2D>> readTimeStack(const std::string& varName);

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

    QTabWidget* tabs_ = nullptr;
    DatasetDock* datasetDock_ = nullptr;
    PlotView2D* plot_ = nullptr;
    MapView* mapView_ = nullptr;
    TileLayer* tileLayer_ = nullptr;
    ColorbarWidget* colorbar_ = nullptr;
    QComboBox* colormapCombo_ = nullptr;
    QCheckBox* autoRangeCheck_ = nullptr;
    QCheckBox* symmetricCheck_ = nullptr;
    QDoubleSpinBox* minSpin_ = nullptr;
    QDoubleSpinBox* maxSpin_ = nullptr;
    QComboBox* levelCombo_ = nullptr;
    QComboBox* derivedCombo_ = nullptr;
    QComboBox* basemapCombo_ = nullptr;
    QCheckBox* contourCheck_ = nullptr;
    QDoubleSpinBox* contourSpin_ = nullptr;
    QSlider* opacitySlider_ = nullptr;
    QCheckBox* graticuleCheck_ = nullptr;
    QCheckBox* coastlineCheck_ = nullptr;
    QComboBox* windCombo_ = nullptr;
    TimeController* timeController_ = nullptr;
    QLabel* probeLabel_ = nullptr;
    QThreadPool* pool_ = nullptr;
};

}  // namespace met::app
