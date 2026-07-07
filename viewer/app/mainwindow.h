#pragma once

#include <memory>
#include <vector>

#include <QMainWindow>

#include "viewer/core/field.h"
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

private slots:
    void onOpenTriggered();
    void onFieldChosen(const core::FieldKey& key);
    void onLevelChanged(int index);
    void onTimeChanged(int index);
    void onColormapChanged(const QString& name);
    void onContoursToggled(bool on);
    void onContourIntervalChanged(double value);
    void onBasemapChanged(int index);
    void onOpacityChanged(int percent);
    void onGraticuleToggled(bool on);
    void onCoastlinesToggled(bool on);
    void onProbeMoved(double lat, double lon, double value, bool hasValue);
    void onProbeLeft();

private:
    void buildUi();
    void decodeCurrent();  // decode the field for the current var/level/time

    std::shared_ptr<readers::IDataset> dataset_;
    QString currentUnits_;
    quint64 generation_ = 0;

    // Current selection state.
    std::string currentVar_;
    std::vector<core::VerticalLevel> currentLevels_;
    std::vector<core::TimePoint> currentTimes_;
    int currentMember_ = -1;
    int levelIdx_ = 0;
    int timeIdx_ = 0;

    QTabWidget* tabs_ = nullptr;
    DatasetDock* datasetDock_ = nullptr;
    PlotView2D* plot_ = nullptr;
    MapView* mapView_ = nullptr;
    TileLayer* tileLayer_ = nullptr;
    ColorbarWidget* colorbar_ = nullptr;
    QComboBox* colormapCombo_ = nullptr;
    QComboBox* levelCombo_ = nullptr;
    QComboBox* basemapCombo_ = nullptr;
    QCheckBox* contourCheck_ = nullptr;
    QDoubleSpinBox* contourSpin_ = nullptr;
    QSlider* opacitySlider_ = nullptr;
    QCheckBox* graticuleCheck_ = nullptr;
    QCheckBox* coastlineCheck_ = nullptr;
    TimeController* timeController_ = nullptr;
    QLabel* probeLabel_ = nullptr;
    QThreadPool* pool_ = nullptr;
};

}  // namespace met::app
