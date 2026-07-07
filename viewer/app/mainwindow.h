#pragma once

#include <memory>

#include <QMainWindow>

#include "viewer/readers/ireader.h"

class QComboBox;
class QLabel;
class QThreadPool;

namespace met::app {

class DatasetDock;
class PlotView2D;
class ColorbarWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

    // Open a file directly (used by the File menu and by command-line args).
    void openFile(const QString& path);

    // Developer/testing aid: after `delayMs`, grab the plot to `pngPath` and
    // quit the application. Used for headless visual verification.
    void scheduleGrabAndQuit(const QString& pngPath, int delayMs = 1200);

private slots:
    void onOpenTriggered();
    void onFieldChosen(const core::FieldKey& key);
    void onColormapChanged(const QString& name);
    void onProbeMoved(double lat, double lon, double value, bool hasValue);
    void onProbeLeft();

private:
    void buildUi();

    std::shared_ptr<readers::IDataset> dataset_;
    QString currentUnits_;
    quint64 generation_ = 0;

    DatasetDock* datasetDock_ = nullptr;
    PlotView2D* plot_ = nullptr;
    ColorbarWidget* colorbar_ = nullptr;
    QComboBox* colormapCombo_ = nullptr;
    QLabel* probeLabel_ = nullptr;
    QThreadPool* pool_ = nullptr;
};

}  // namespace met::app
