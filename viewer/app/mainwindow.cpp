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
#include <QSlider>
#include <QStatusBar>
#include <QTabWidget>
#include <QThreadPool>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <QActionGroup>
#include <QCloseEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QSettings>
#include <QSpinBox>
#include <QToolBar>

#include "viewer/analysis/crosssection.h"
#include "viewer/analysis/derived.h"
#include "viewer/analysis/sounding.h"
#include "viewer/analysis/timeseries.h"
#include "viewer/app/coastlines.h"
#include "viewer/app/colorbarwidget.h"
#include "viewer/app/crosssectionview.h"
#include "viewer/app/datasetdock.h"
#include "viewer/app/jobs.h"
#include "viewer/app/mapview.h"
#include "viewer/app/plotview2d.h"
#include "viewer/app/skewtview.h"
#include "viewer/app/tilelayer.h"
#include "viewer/app/timecontroller.h"
#include "viewer/app/timeseriesview.h"
#include "viewer/core/timeaxis.h"
#include "viewer/core/units.h"
#include "viewer/readers/detect.h"

namespace met::app {

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(tr("met-viewer"));
    resize(1280, 800);
    pool_ = new QThreadPool(this);
    buildUi();
    loadSettings();
}

void MainWindow::buildUi() {
    // Central: tabbed 2D plot + GIS map, both fed by the current field.
    plot_ = new PlotView2D(this);
    tileLayer_ = new TileLayer(this);
    mapView_ = new MapView(tileLayer_, this);
    mapView_->setCoastlines(loadCoastlines(":/coastlines/ne_coastline_110m.bin"));

    tabs_ = new QTabWidget(this);
    tabs_->addTab(plot_, tr("2D Plot"));
    tabs_->addTab(mapView_, tr("Map"));
    setCentralWidget(tabs_);

    connect(plot_, &PlotView2D::probeMoved, this, &MainWindow::onProbeMoved);
    connect(plot_, &PlotView2D::probeLeft, this, &MainWindow::onProbeLeft);
    connect(mapView_, &MapView::probeMoved, this, &MainWindow::onProbeMoved);
    connect(mapView_, &MapView::probeLeft, this, &MainWindow::onProbeLeft);
    connect(mapView_, &MapView::crossSectionRequested, this, &MainWindow::onCrossSectionRequested);
    connect(mapView_, &MapView::soundingRequested, this, &MainWindow::onSoundingRequested);
    connect(mapView_, &MapView::timeSeriesRequested, this, &MainWindow::onTimeSeriesRequested);

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

    derivedCombo_ = new QComboBox(controls);
    derivedCombo_->addItems({tr("(raw field)"), tr("Wind speed"), tr("Wind direction"),
                             tr("Rel. vorticity"), tr("Divergence"), tr("Potential temp θ")});
    connect(derivedCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this,
            &MainWindow::onDerivedChanged);
    grid->addRow(tr("Derived"), derivedCombo_);

    colormapCombo_ = new QComboBox(controls);
    for (const auto& name : render::Colormap::builtinNames())
        colormapCombo_->addItem(QString::fromStdString(name));
    connect(colormapCombo_, &QComboBox::currentTextChanged, this, &MainWindow::onColormapChanged);
    grid->addRow(tr("Colormap"), colormapCombo_);

    autoRangeCheck_ = new QCheckBox(tr("Auto range"), controls);
    autoRangeCheck_->setChecked(true);
    connect(autoRangeCheck_, &QCheckBox::toggled, this, &MainWindow::onAutoRangeToggled);
    grid->addRow(QString(), autoRangeCheck_);

    symmetricCheck_ = new QCheckBox(tr("Symmetric (0-centered)"), controls);
    connect(symmetricCheck_, &QCheckBox::toggled, this, &MainWindow::onSymmetricToggled);
    grid->addRow(QString(), symmetricCheck_);

    minSpin_ = new QDoubleSpinBox(controls);
    minSpin_->setRange(-1e12, 1e12);
    minSpin_->setDecimals(4);
    minSpin_->setEnabled(false);
    connect(minSpin_, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
            &MainWindow::onRangeSpinChanged);
    grid->addRow(tr("Min"), minSpin_);

    maxSpin_ = new QDoubleSpinBox(controls);
    maxSpin_->setRange(-1e12, 1e12);
    maxSpin_->setDecimals(4);
    maxSpin_->setEnabled(false);
    connect(maxSpin_, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
            &MainWindow::onRangeSpinChanged);
    grid->addRow(tr("Max"), maxSpin_);

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

    // Map-specific controls.
    basemapCombo_ = new QComboBox(controls);
    for (const auto& src : TileLayer::builtinSources()) basemapCombo_->addItem(src.name);
    connect(basemapCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this,
            &MainWindow::onBasemapChanged);
    grid->addRow(tr("Basemap"), basemapCombo_);

    opacitySlider_ = new QSlider(Qt::Horizontal, controls);
    opacitySlider_->setRange(0, 100);
    opacitySlider_->setValue(75);
    connect(opacitySlider_, &QSlider::valueChanged, this, &MainWindow::onOpacityChanged);
    grid->addRow(tr("Field opacity"), opacitySlider_);

    graticuleCheck_ = new QCheckBox(tr("Graticule"), controls);
    graticuleCheck_->setChecked(true);
    connect(graticuleCheck_, &QCheckBox::toggled, this, &MainWindow::onGraticuleToggled);
    grid->addRow(QString(), graticuleCheck_);

    coastlineCheck_ = new QCheckBox(tr("Coastlines"), controls);
    coastlineCheck_->setChecked(true);
    connect(coastlineCheck_, &QCheckBox::toggled, this, &MainWindow::onCoastlinesToggled);
    grid->addRow(QString(), coastlineCheck_);

    gpuCheck_ = new QCheckBox(tr("GPU render (experimental)"), controls);
    gpuCheck_->setChecked(false);
    connect(gpuCheck_, &QCheckBox::toggled, this, &MainWindow::onGpuToggled);
    grid->addRow(QString(), gpuCheck_);

    windCombo_ = new QComboBox(controls);
    windCombo_->addItems({tr("Off"), tr("Barbs"), tr("Streamlines")});
    connect(windCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this,
            &MainWindow::onWindModeChanged);
    grid->addRow(tr("Wind"), windCombo_);

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
    QAction* prefAct = fileMenu->addAction(tr("&Preferences…"));
    connect(prefAct, &QAction::triggered, this, &MainWindow::openPreferences);
    fileMenu->addSeparator();
    QAction* quitAct = fileMenu->addAction(tr("&Quit"));
    quitAct->setShortcut(QKeySequence::Quit);
    connect(quitAct, &QAction::triggered, this, &QWidget::close);

    // Interaction-mode toolbar (map picking).
    auto* toolbar = addToolBar(tr("Tools"));
    toolbar->setObjectName("toolsToolbar");
    auto* modeGroup = new QActionGroup(this);
    modeGroup->setExclusive(true);
    struct ModeDef { const char* label; MapView::Mode mode; };
    const ModeDef modes[] = {{"Pan", MapView::Mode::Pan},
                             {"Cross-section", MapView::Mode::CrossSection},
                             {"Sounding", MapView::Mode::Sounding},
                             {"Time series", MapView::Mode::TimeSeries}};
    for (const auto& m : modes) {
        QAction* act = toolbar->addAction(tr(m.label));
        act->setCheckable(true);
        modeGroup->addAction(act);
        const MapView::Mode mode = m.mode;
        connect(act, &QAction::triggered, this, [this, mode]() {
            mapView_->setInteractionMode(mode);
            if (mode != MapView::Mode::Pan) tabs_->setCurrentWidget(mapView_);
        });
        if (m.mode == MapView::Mode::Pan) act->setChecked(true);
    }

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
    fieldCache_.clear();  // a new file invalidates cached fields
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

    // Cache hit: display immediately (no decode) — this is what makes scrubbing
    // and animation smooth.
    if (auto cached = fieldCache_.get(key)) {
        currentRaw_ = cached;
        presentField();
        prefetchAhead();
        return;
    }

    probeLabel_->setText(tr("Decoding…"));
    submitDecode(*pool_, dataset_, key, gen, this, [this, gen, key](DecodeOutcome outcome) {
        if (!outcome.field) {
            if (outcome.generation == generation_)
                probeLabel_->setText(tr("Decode error: %1").arg(outcome.error));
            return;
        }
        fieldCache_.put(key, outcome.field);
        if (outcome.generation != generation_) return;  // superseded; kept in cache
        currentRaw_ = outcome.field;
        presentField();
        prefetchAhead();
    });
}

void MainWindow::presentField() {
    if (!currentRaw_) return;
    if (derivedMode_ == 0) {
        displayField(currentRaw_);
        return;
    }

    // Derived quantities.
    std::shared_ptr<core::Field2D> derived;
    if (derivedMode_ == 5) {
        // Potential temperature from the current field (assumed temperature).
        const auto& lvl = currentRaw_->meta.level;
        if (lvl.type == core::VerticalLevel::Type::PressureHPa)
            derived = std::make_shared<core::Field2D>(
                analysis::potentialTemperatureField(*currentRaw_, lvl.value));
    } else {
        auto wind = buildWindField();
        if (wind) {
            switch (derivedMode_) {
                case 1: derived = std::make_shared<core::Field2D>(analysis::windSpeedField(*wind)); break;
                case 2: derived = std::make_shared<core::Field2D>(analysis::windDirectionField(*wind)); break;
                case 3: derived = std::make_shared<core::Field2D>(analysis::relativeVorticityField(*wind)); break;
                case 4: derived = std::make_shared<core::Field2D>(analysis::divergenceField(*wind)); break;
                default: break;
            }
        }
    }

    if (derived) {
        displayField(derived);
    } else {
        statusBar()->showMessage(tr("Derived quantity unavailable here — showing raw field"), 3000);
        displayField(currentRaw_);
    }
}

void MainWindow::onDerivedChanged(int index) {
    derivedMode_ = index;
    // Signed fields (vorticity/divergence) read best on a diverging map centered
    // at zero — switch to it automatically unless the user already chose one.
    const bool signedField = (index == 3 || index == 4);
    if (signedField && !render::Colormap::isDiverging(colormapCombo_->currentText().toStdString())) {
        colormapCombo_->setCurrentText("RdBu (diverging)");  // triggers onColormapChanged
        symmetricCheck_->setChecked(true);                   // triggers applyRange
    }
    presentField();
}

void MainWindow::displayField(std::shared_ptr<core::Field2D> field) {
    currentUnits_ = QString::fromStdString(field->meta.units);
    plot_->setField(field);
    mapView_->setField(field);
    colorbar_->setUnits(currentUnits_);
    applyRange();  // apply auto/manual/symmetric range and sync the colorbar
    probeLabel_->setText(tr("%1 @ %2 — %3×%4")
                             .arg(QString::fromStdString(field->meta.varName))
                             .arg(QString::fromStdString(met::core::formatLevel(field->meta.level)))
                             .arg(field->width())
                             .arg(field->height()));
    updateWind();
}

void MainWindow::prefetchAhead() {
    if (!dataset_ || currentTimes_.size() < 2) return;
    const int n = static_cast<int>(currentTimes_.size());
    for (int step = 1; step <= prefetchAhead_ && step < n; ++step) {
        core::FieldKey key;
        key.varName = currentVar_;
        key.level = currentLevels_[static_cast<std::size_t>(levelIdx_)];
        key.validTime = currentTimes_[static_cast<std::size_t>((timeIdx_ + step) % n)];
        key.member = currentMember_;
        if (fieldCache_.contains(key)) continue;
        // Prefetch jobs never display (generation 0); they only warm the cache.
        submitDecode(*pool_, dataset_, key, 0, this, [this, key](DecodeOutcome outcome) {
            if (outcome.field) fieldCache_.put(key, outcome.field);
        });
    }
}

std::vector<std::pair<double, core::Field2D>> MainWindow::readLevelStack(
    const std::string& varName) {
    std::vector<std::pair<double, core::Field2D>> stack;
    const auto* entry = dataset_ ? dataset_->catalog().find(varName) : nullptr;
    if (!entry || currentTimes_.empty()) return stack;
    const core::TimePoint time = currentTimes_[static_cast<std::size_t>(timeIdx_)];
    for (const auto& lvl : entry->levels) {
        if (lvl.type != core::VerticalLevel::Type::PressureHPa) continue;
        try {
            stack.emplace_back(lvl.value,
                               dataset_->readField(core::FieldKey{varName, lvl, time, currentMember_}));
        } catch (const std::exception&) {
        }
    }
    return stack;
}

std::vector<std::pair<core::TimePoint, core::Field2D>> MainWindow::readTimeStack(
    const std::string& varName) {
    std::vector<std::pair<core::TimePoint, core::Field2D>> stack;
    const auto* entry = dataset_ ? dataset_->catalog().find(varName) : nullptr;
    if (!entry || currentLevels_.empty()) return stack;
    const core::VerticalLevel level = currentLevels_[static_cast<std::size_t>(levelIdx_)];
    for (const auto& t : entry->times) {
        try {
            stack.emplace_back(t,
                               dataset_->readField(core::FieldKey{varName, level, t, currentMember_}));
        } catch (const std::exception&) {
        }
    }
    return stack;
}

void MainWindow::onCrossSectionRequested(const std::vector<core::LatLon>& path) {
    if (currentVar_.empty()) return;
    const auto stack = readLevelStack(currentVar_);
    if (stack.size() < 2) {
        statusBar()->showMessage(tr("Cross-section needs a multi-level variable"), 4000);
        return;
    }
    const analysis::CrossSection cs = analysis::extractCrossSection(stack, path, 200);
    auto* view = new CrossSectionView;
    view->setSection(cs);
    const int idx = tabs_->addTab(view, tr("Section: %1").arg(QString::fromStdString(currentVar_)));
    tabs_->setCurrentIndex(idx);
}

void MainWindow::onSoundingRequested(core::LatLon point) {
    // Temperature is required; relative humidity is optional (for dewpoint).
    const auto tStack = readLevelStack("t");
    if (tStack.size() < 2) {
        statusBar()->showMessage(tr("Sounding needs multi-level temperature (t)"), 4000);
        return;
    }
    const auto rhStack = readLevelStack("r");
    const analysis::Sounding s = analysis::extractSounding(tStack, rhStack, point);
    auto* view = new SkewTView;
    view->setSounding(s);
    const int idx = tabs_->addTab(view, tr("Skew-T"));
    tabs_->setCurrentIndex(idx);
}

void MainWindow::onTimeSeriesRequested(core::LatLon point) {
    if (currentVar_.empty()) return;
    const auto stack = readTimeStack(currentVar_);
    if (stack.size() < 2) {
        statusBar()->showMessage(tr("Time series needs multiple time steps"), 4000);
        return;
    }
    const analysis::TimeSeries ts = analysis::extractTimeSeries(stack, point);
    auto* view = new TimeSeriesView;
    view->setSeries(ts, QString::fromStdString(currentVar_));
    const int idx = tabs_->addTab(view, tr("Series: %1").arg(QString::fromStdString(currentVar_)));
    tabs_->setCurrentIndex(idx);
}

void MainWindow::demoCrossSection() {
    onCrossSectionRequested({{68.0, 4.0}, {58.0, 26.0}});
}
void MainWindow::demoSounding() { onSoundingRequested({64.0, 12.0}); }
void MainWindow::demoTimeSeries() { onTimeSeriesRequested({64.0, 12.0}); }

void MainWindow::onColormapChanged(const QString& name) {
    plot_->setColormapByName(name);
    mapView_->setColormapByName(name);
    applyRange();  // re-apply the current range through the new colormap
    if (plot_->hasField()) {
        colorbar_->setColormap(plot_->colormap());
        colorbar_->setUnits(currentUnits_);
    }
}

void MainWindow::applyRange() {
    const bool autoOn = autoRangeCheck_ && autoRangeCheck_->isChecked();
    minSpin_->setEnabled(!autoOn);
    maxSpin_->setEnabled(!autoOn);

    if (autoOn) {
        plot_->setAutoRange(true);
        mapView_->setAutoRange(true);
        // A symmetric auto-range centers the field's extent on zero (for signed
        // fields with a diverging map).
        if (symmetricCheck_ && symmetricCheck_->isChecked() && plot_->hasField()) {
            const double a = std::max(std::abs(plot_->colormap().min()),
                                      std::abs(plot_->colormap().max()));
            plot_->setAutoRange(false);
            mapView_->setAutoRange(false);
            plot_->setRange(-a, a);
            mapView_->setRange(-a, a);
        }
        syncRangeSpins();
    } else {
        double lo = minSpin_->value();
        double hi = maxSpin_->value();
        if (symmetricCheck_ && symmetricCheck_->isChecked()) {
            const double a = std::max(std::abs(lo), std::abs(hi));
            lo = -a;
            hi = a;
        }
        if (hi <= lo) hi = lo + 1.0;
        plot_->setAutoRange(false);
        mapView_->setAutoRange(false);
        plot_->setRange(lo, hi);
        mapView_->setRange(lo, hi);
    }
    if (plot_->hasField()) colorbar_->setColormap(plot_->colormap());
}

void MainWindow::syncRangeSpins() {
    QSignalBlocker b1(minSpin_), b2(maxSpin_);
    minSpin_->setValue(plot_->colormap().min());
    maxSpin_->setValue(plot_->colormap().max());
}

void MainWindow::onAutoRangeToggled(bool /*on*/) { applyRange(); }
void MainWindow::onSymmetricToggled(bool /*on*/) { applyRange(); }
void MainWindow::onRangeSpinChanged() {
    if (autoRangeCheck_ && !autoRangeCheck_->isChecked()) applyRange();
}

void MainWindow::setContoursChecked(bool on) { contourCheck_->setChecked(on); }

void MainWindow::showMapTab() {
    if (tabs_) tabs_->setCurrentWidget(mapView_);
}

void MainWindow::setWindComboIndex(int index) {
    if (windCombo_) windCombo_->setCurrentIndex(index);
}

void MainWindow::startPlayback() { timeController_->play(); }

void MainWindow::setGpuChecked(bool on) {
    if (gpuCheck_) gpuCheck_->setChecked(on);
}

void MainWindow::setDerivedComboIndex(int index) {
    if (derivedCombo_) derivedCombo_->setCurrentIndex(index);
}

void MainWindow::onContoursToggled(bool on) { plot_->setContoursEnabled(on); }

void MainWindow::onContourIntervalChanged(double value) { plot_->setContourInterval(value); }

void MainWindow::onBasemapChanged(int index) {
    const auto sources = TileLayer::builtinSources();
    if (index < 0 || index >= sources.size()) return;
    tileLayer_->setSource(sources.at(index));
    mapView_->refreshSource();
}

void MainWindow::onOpacityChanged(int percent) { mapView_->setOpacity(percent / 100.0); }

void MainWindow::onGraticuleToggled(bool on) { mapView_->setGraticuleVisible(on); }

void MainWindow::onCoastlinesToggled(bool on) { mapView_->setCoastlinesVisible(on); }

void MainWindow::onGpuToggled(bool on) { mapView_->setGpuEnabled(on); }

void MainWindow::onWindModeChanged(int index) {
    mapView_->setWindMode(index);
    plot_->setWindMode(index);
    updateWind();
}

std::shared_ptr<analysis::WindField> MainWindow::buildWindField() {
    if (!dataset_ || currentLevels_.empty() || currentTimes_.empty()) return nullptr;
    std::vector<std::string> names;
    for (const auto& v : dataset_->catalog().variables()) names.push_back(v.varName);
    const auto pair = analysis::findWindPair(names);
    if (!pair) return nullptr;

    const core::VerticalLevel level = currentLevels_[static_cast<std::size_t>(levelIdx_)];
    const core::TimePoint time = currentTimes_[static_cast<std::size_t>(timeIdx_)];
    try {
        auto wind = std::make_shared<analysis::WindField>();
        wind->u = dataset_->readField(core::FieldKey{pair->uName, level, time, currentMember_});
        wind->v = dataset_->readField(core::FieldKey{pair->vName, level, time, currentMember_});
        analysis::rotateToEarthRelative(*wind);
        return wind;
    } catch (const std::exception&) {
        return nullptr;  // U/V may not exist at this level/time
    }
}

void MainWindow::updateWind() {
    if (!windCombo_ || windCombo_->currentIndex() == 0) {
        mapView_->setWind(nullptr);
        plot_->setWind(nullptr);
        return;
    }
    auto wind = buildWindField();
    if (!wind) statusBar()->showMessage(tr("No U/V wind pair at this level/time"), 3000);
    mapView_->setWind(wind);
    plot_->setWind(wind);
}

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

void MainWindow::loadSettings() {
    QSettings s;
    if (s.contains("geometry")) restoreGeometry(s.value("geometry").toByteArray());
    if (s.contains("windowState")) restoreState(s.value("windowState").toByteArray());

    const QString cmap = s.value("colormap", "viridis").toString();
    const int ci = colormapCombo_->findText(cmap);
    if (ci >= 0) colormapCombo_->setCurrentIndex(ci);
    const int bi = s.value("basemap", 0).toInt();
    if (bi >= 0 && bi < basemapCombo_->count()) basemapCombo_->setCurrentIndex(bi);
    opacitySlider_->setValue(s.value("opacity", 75).toInt());
    graticuleCheck_->setChecked(s.value("graticule", true).toBool());
    coastlineCheck_->setChecked(s.value("coastlines", true).toBool());

    cacheBudgetMB_ = s.value("cacheBudgetMB", 1024).toInt();
    fieldCache_.setBudgetBytes(static_cast<std::size_t>(cacheBudgetMB_) * 1024 * 1024);
    animationFps_ = s.value("animationFps", 6).toInt();
    timeController_->setFps(animationFps_);
}

void MainWindow::saveSettings() {
    QSettings s;
    s.setValue("geometry", saveGeometry());
    s.setValue("windowState", saveState());
    s.setValue("colormap", colormapCombo_->currentText());
    s.setValue("basemap", basemapCombo_->currentIndex());
    s.setValue("opacity", opacitySlider_->value());
    s.setValue("graticule", graticuleCheck_->isChecked());
    s.setValue("coastlines", coastlineCheck_->isChecked());
    s.setValue("cacheBudgetMB", cacheBudgetMB_);
    s.setValue("animationFps", animationFps_);
}

void MainWindow::openPreferences() {
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Preferences"));
    auto* form = new QFormLayout(&dlg);

    auto* cacheSpin = new QSpinBox(&dlg);
    cacheSpin->setRange(16, 16384);
    cacheSpin->setSuffix(tr(" MB"));
    cacheSpin->setValue(cacheBudgetMB_);
    form->addRow(tr("Field cache budget"), cacheSpin);

    auto* fpsSpin = new QSpinBox(&dlg);
    fpsSpin->setRange(1, 30);
    fpsSpin->setSuffix(tr(" fps"));
    fpsSpin->setValue(animationFps_);
    form->addRow(tr("Animation speed"), fpsSpin);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() == QDialog::Accepted) {
        cacheBudgetMB_ = cacheSpin->value();
        fieldCache_.setBudgetBytes(static_cast<std::size_t>(cacheBudgetMB_) * 1024 * 1024);
        animationFps_ = fpsSpin->value();
        timeController_->setFps(animationFps_);
    }
}

void MainWindow::closeEvent(QCloseEvent* event) {
    saveSettings();
    QMainWindow::closeEvent(event);
}

}  // namespace met::app
