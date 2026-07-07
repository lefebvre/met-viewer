#include "viewer/app/mainwindow.h"

#include <cmath>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QThreadPool>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include "viewer/app/colorbarwidget.h"
#include "viewer/app/datasetdock.h"
#include "viewer/app/jobs.h"
#include "viewer/app/plotview2d.h"
#include "viewer/app/timecontroller.h"
#include "viewer/core/timeaxis.h"
#include "viewer/core/units.h"
#include "viewer/readers/detect.h"

namespace met::app {

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(tr("met-viewer"));
    resize(1280, 800);
    pool_ = new QThreadPool(this);
    buildUi();
}

void MainWindow::buildUi() {
    plot_ = new PlotView2D(this);
    setCentralWidget(plot_);
    connect(plot_, &PlotView2D::probeMoved, this, &MainWindow::onProbeMoved);
    connect(plot_, &PlotView2D::probeLeft, this, &MainWindow::onProbeLeft);

    // Left: dataset browser.
    datasetDock_ = new DatasetDock(this);
    auto* leftDock = new QDockWidget(tr("Dataset"), this);
    leftDock->setObjectName("datasetDock");
    leftDock->setWidget(datasetDock_);
    addDockWidget(Qt::LeftDockWidgetArea, leftDock);
    connect(datasetDock_, &DatasetDock::fieldChosen, this, &MainWindow::onFieldChosen);

    // Right: inspector.
    auto* inspector = new QWidget(this);
    auto* form = new QVBoxLayout(inspector);
    auto* controls = new QWidget(inspector);
    auto* grid = new QFormLayout(controls);
    grid->setContentsMargins(0, 0, 0, 0);

    levelCombo_ = new QComboBox(controls);
    connect(levelCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this,
            &MainWindow::onLevelChanged);
    grid->addRow(tr("Level"), levelCombo_);

    colormapCombo_ = new QComboBox(controls);
    for (const auto& name : render::Colormap::builtinNames())
        colormapCombo_->addItem(QString::fromStdString(name));
    connect(colormapCombo_, &QComboBox::currentTextChanged, this, &MainWindow::onColormapChanged);
    grid->addRow(tr("Colormap"), colormapCombo_);

    contourCheck_ = new QCheckBox(tr("Contours"), controls);
    connect(contourCheck_, &QCheckBox::toggled, this, &MainWindow::onContoursToggled);
    grid->addRow(QString(), contourCheck_);

    contourSpin_ = new QDoubleSpinBox(controls);
    contourSpin_->setRange(0.0, 1e6);
    contourSpin_->setDecimals(3);
    contourSpin_->setValue(0.0);
    contourSpin_->setSpecialValueText(tr("auto"));
    connect(contourSpin_, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
            &MainWindow::onContourIntervalChanged);
    grid->addRow(tr("Interval"), contourSpin_);

    colorbar_ = new ColorbarWidget(inspector);
    form->addWidget(controls);
    form->addWidget(colorbar_, 1);

    auto* rightDock = new QDockWidget(tr("Inspector"), this);
    rightDock->setObjectName("inspectorDock");
    rightDock->setWidget(inspector);
    addDockWidget(Qt::RightDockWidgetArea, rightDock);

    // Bottom: time controller.
    timeController_ = new TimeController(this);
    auto* bottomDock = new QDockWidget(tr("Time"), this);
    bottomDock->setObjectName("timeDock");
    bottomDock->setWidget(timeController_);
    addDockWidget(Qt::BottomDockWidgetArea, bottomDock);
    connect(timeController_, &TimeController::indexChanged, this, &MainWindow::onTimeChanged);

    // Menu.
    auto* fileMenu = menuBar()->addMenu(tr("&File"));
    QAction* openAct = fileMenu->addAction(tr("&Open…"));
    openAct->setShortcut(QKeySequence::Open);
    connect(openAct, &QAction::triggered, this, &MainWindow::onOpenTriggered);
    QAction* quitAct = fileMenu->addAction(tr("&Quit"));
    quitAct->setShortcut(QKeySequence::Quit);
    connect(quitAct, &QAction::triggered, this, &QWidget::close);

    probeLabel_ = new QLabel(tr("Ready"), this);
    statusBar()->addWidget(probeLabel_);
}

void MainWindow::scheduleGrabAndQuit(const QString& pngPath, int delayMs) {
    QTimer::singleShot(delayMs, this, [this, pngPath]() {
        grab().save(pngPath);
        QApplication::quit();
    });
}

void MainWindow::onOpenTriggered() {
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open meteorological file"), {},
        tr("Meteorological data (*.grib *.grib2 *.grb *.grb2 *.grib1 *.nc *.nc4);;All files (*)"));
    if (!path.isEmpty()) openFile(path);
}

void MainWindow::openFile(const QString& path) {
    try {
        std::unique_ptr<readers::IDataset> ds =
            readers::openDataset(std::filesystem::path(path.toStdString()));
        dataset_ = std::shared_ptr<readers::IDataset>(std::move(ds));
    } catch (const std::exception& e) {
        QMessageBox::warning(this, tr("Open failed"), QString::fromUtf8(e.what()));
        return;
    }
    datasetDock_->setCatalog(dataset_->catalog());
    plot_->clearField();
    colorbar_->clear();
    statusBar()->showMessage(
        tr("Opened %1 (%2)").arg(path).arg(QString::fromStdString(dataset_->formatName())), 4000);

    // Auto-display the first available field.
    const auto& vars = dataset_->catalog().variables();
    if (!vars.empty() && !vars.front().levels.empty()) {
        const auto& v = vars.front();
        core::FieldKey key;
        key.varName = v.varName;
        key.level = v.levels.front();
        key.validTime = v.times.empty() ? core::TimePoint{} : v.times.front();
        key.member = v.members.empty() ? -1 : v.members.front();
        onFieldChosen(key);
    }
}

void MainWindow::onFieldChosen(const core::FieldKey& key) {
    if (!dataset_) return;
    const auto* entry = dataset_->catalog().find(key.varName);
    if (!entry) return;

    currentVar_ = key.varName;
    currentLevels_ = entry->levels;
    currentTimes_ = entry->times;
    currentMember_ = entry->members.empty() ? -1 : entry->members.front();

    // Resolve indices of the chosen level/time.
    levelIdx_ = 0;
    for (std::size_t i = 0; i < currentLevels_.size(); ++i)
        if (currentLevels_[i] == key.level) levelIdx_ = static_cast<int>(i);
    timeIdx_ = 0;
    for (std::size_t i = 0; i < currentTimes_.size(); ++i)
        if (currentTimes_[i] == key.validTime) timeIdx_ = static_cast<int>(i);

    // Populate the level combo without triggering a redundant decode.
    {
        QSignalBlocker block(levelCombo_);
        levelCombo_->clear();
        for (const auto& lvl : currentLevels_)
            levelCombo_->addItem(QString::fromStdString(met::core::formatLevel(lvl)));
        levelCombo_->setCurrentIndex(levelIdx_);
    }

    // Populate the time controller.
    QStringList times;
    for (const auto& t : currentTimes_) times << QString::fromStdString(met::core::formatTime(t));
    {
        QSignalBlocker block(timeController_);
        timeController_->setSteps(times, timeIdx_);
    }

    decodeCurrent();
}

void MainWindow::onLevelChanged(int index) {
    if (index < 0 || index >= static_cast<int>(currentLevels_.size())) return;
    levelIdx_ = index;
    decodeCurrent();
}

void MainWindow::onTimeChanged(int index) {
    if (index < 0 || index >= static_cast<int>(currentTimes_.size())) return;
    timeIdx_ = index;
    decodeCurrent();
}

void MainWindow::decodeCurrent() {
    if (!dataset_ || currentVar_.empty() || currentLevels_.empty() || currentTimes_.empty()) return;

    core::FieldKey key;
    key.varName = currentVar_;
    key.level = currentLevels_[static_cast<std::size_t>(levelIdx_)];
    key.validTime = currentTimes_[static_cast<std::size_t>(timeIdx_)];
    key.member = currentMember_;

    const quint64 gen = ++generation_;
    probeLabel_->setText(tr("Decoding…"));

    submitDecode(*pool_, dataset_, key, gen, this, [this, gen](DecodeOutcome outcome) {
        if (outcome.generation != generation_) return;  // stale
        if (!outcome.field) {
            probeLabel_->setText(tr("Decode error: %1").arg(outcome.error));
            return;
        }
        currentUnits_ = QString::fromStdString(outcome.field->meta.units);
        plot_->setField(outcome.field);
        colorbar_->setColormap(plot_->colormap());
        colorbar_->setUnits(currentUnits_);
        probeLabel_->setText(tr("%1 @ %2 — %3×%4")
                                 .arg(QString::fromStdString(outcome.field->meta.varName))
                                 .arg(QString::fromStdString(
                                     met::core::formatLevel(outcome.field->meta.level)))
                                 .arg(outcome.field->width())
                                 .arg(outcome.field->height()));
    });
}

void MainWindow::onColormapChanged(const QString& name) {
    plot_->setColormapByName(name);
    if (plot_->hasField()) {
        colorbar_->setColormap(plot_->colormap());
        colorbar_->setUnits(currentUnits_);
    }
}

void MainWindow::setContoursChecked(bool on) { contourCheck_->setChecked(on); }

void MainWindow::onContoursToggled(bool on) { plot_->setContoursEnabled(on); }

void MainWindow::onContourIntervalChanged(double value) { plot_->setContourInterval(value); }

void MainWindow::onProbeMoved(double lat, double lon, double value, bool hasValue) {
    QString s = QStringLiteral("lat %1°  lon %2°").arg(lat, 0, 'f', 2).arg(lon, 0, 'f', 2);
    if (hasValue) {
        s += QStringLiteral("   %1 %2").arg(value, 0, 'f', 2).arg(currentUnits_);
        const auto alt = met::core::preferredDisplayUnit(currentUnits_.toStdString());
        if (alt) {
            const auto converted = met::core::convert(value, currentUnits_.toStdString(), *alt);
            if (converted)
                s += QStringLiteral(" (%1 %2)")
                         .arg(*converted, 0, 'f', 2)
                         .arg(QString::fromStdString(met::core::unitLabel(*alt)));
        }
    } else {
        s += QStringLiteral("   (no data)");
    }
    probeLabel_->setText(s);
}

void MainWindow::onProbeLeft() {
    if (plot_->hasField()) probeLabel_->setText(tr("—"));
}

}  // namespace met::app
