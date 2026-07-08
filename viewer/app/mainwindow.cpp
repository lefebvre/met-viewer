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
#include <QTabBar>
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
#include "viewer/app/controlpanel.h"
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
namespace {

// Add colormap + auto/manual range controls and a colorbar legend to `panel`,
// wired to `view` (which must expose setColormapByName/setAutoRange/setRange/
// colormap()/units() and a rangeChanged(double,double) signal). Returns the
// colormap combo so the caller can persist / drive it programmatically. Shared by
// the 2D-Plot, Map, and cross-section panels so their color controls behave alike.
template <typename View>
QComboBox* addColormapControls(ControlPanel* panel, View* view) {
    auto* cmap = new QComboBox(panel);
    for (const auto& name : render::Colormap::builtinNames())
        cmap->addItem(QString::fromStdString(name));
    cmap->setCurrentText(QString::fromStdString(view->colormap().name()));

    auto* autoR = new QCheckBox(QObject::tr("Auto range"), panel);
    autoR->setChecked(true);
    auto* minS = new QDoubleSpinBox(panel);
    minS->setRange(-1e12, 1e12);
    minS->setDecimals(3);
    minS->setEnabled(false);
    auto* maxS = new QDoubleSpinBox(panel);
    maxS->setRange(-1e12, 1e12);
    maxS->setDecimals(3);
    maxS->setEnabled(false);

    auto* cbar = new ColorbarWidget(panel);
    cbar->setColormap(view->colormap());

    panel->addRow(QObject::tr("Colormap"), cmap);
    panel->addRow(autoR);
    panel->addRow(QObject::tr("Min"), minS);
    panel->addRow(QObject::tr("Max"), maxS);
    panel->addBlock(cbar);

    QObject::connect(cmap, &QComboBox::currentTextChanged, view, [view, cbar](const QString& n) {
        view->setColormapByName(n);
        cbar->setColormap(view->colormap());
    });
    QObject::connect(autoR, &QCheckBox::toggled, view, [view, minS, maxS](bool on) {
        view->setAutoRange(on);
        minS->setEnabled(!on);
        maxS->setEnabled(!on);
    });
    auto pushManual = [view, minS, maxS, autoR] {
        if (!autoR->isChecked()) view->setRange(minS->value(), maxS->value());
    };
    QObject::connect(minS, qOverload<double>(&QDoubleSpinBox::valueChanged), view, pushManual);
    QObject::connect(maxS, qOverload<double>(&QDoubleSpinBox::valueChanged), view, pushManual);
    QObject::connect(view, &View::rangeChanged, view,
                     [view, cbar, minS, maxS](double lo, double hi) {
                         cbar->setColormap(view->colormap());
                         cbar->setUnits(view->units());
                         const QSignalBlocker b1(minS), b2(maxS);
                         minS->setValue(lo);
                         maxS->setValue(hi);
                     });
    return cmap;
}

}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(tr("met-viewer"));
    resize(1280, 800);
    pool_ = new QThreadPool(this);
    buildUi();
    loadSettings();
}

void MainWindow::buildUi() {
    // Center: tabbed 2D plot + GIS map, each wrapped in a ViewFrame whose own
    // control panel owns that view's display controls (contextual by construction).
    plot_ = new PlotView2D(this);
    tileLayer_ = new TileLayer(this);
    mapView_ = new MapView(tileLayer_, this);
    mapView_->setCoastlines(loadCoastlines(":/coastlines/ne_coastline_110m.bin"));

    tabs_ = new QTabWidget(this);
    plotFrame_ = buildPlotFrame();
    mapFrame_ = buildMapFrame();
    tabs_->addTab(plotFrame_, tr("2D Plot"));
    tabs_->addTab(mapFrame_, tr("Map"));
    setCentralWidget(tabs_);

    // Analysis views (section/sounding/series) are added as closable tabs; the two
    // base tabs stay permanent (strip their close buttons on both button sides).
    tabs_->setTabsClosable(true);
    for (int side = 0; side <= 1; ++side) {
        const auto pos = static_cast<QTabBar::ButtonPosition>(side);
        tabs_->tabBar()->setTabButton(0, pos, nullptr);
        tabs_->tabBar()->setTabButton(1, pos, nullptr);
    }
    connect(tabs_, &QTabWidget::tabCloseRequested, this, &MainWindow::onTabCloseRequested);

    connect(plot_, &PlotView2D::probeMoved, this, &MainWindow::onProbeMoved);
    connect(plot_, &PlotView2D::probeLeft, this, &MainWindow::onProbeLeft);
    connect(mapView_, &MapView::probeMoved, this, &MainWindow::onProbeMoved);
    connect(mapView_, &MapView::probeLeft, this, &MainWindow::onProbeLeft);
    connect(mapView_, &MapView::crossSectionRequested, this, &MainWindow::onCrossSectionRequested);
    connect(mapView_, &MapView::soundingRequested, this, &MainWindow::onSoundingRequested);
    connect(mapView_, &MapView::timeSeriesRequested, this, &MainWindow::onTimeSeriesRequested);

    // Left: Data dock — the variable tree plus the global level/derived selection
    // (what data the field views show) and a live "Showing:" summary.
    datasetDock_ = new DatasetDock(this);
    connect(datasetDock_, &DatasetDock::fieldChosen, this, &MainWindow::onFieldChosen);

    auto* dataPanel = new QWidget(this);
    auto* dataLayout = new QVBoxLayout(dataPanel);
    dataLayout->setContentsMargins(0, 0, 0, 0);
    dataLayout->addWidget(datasetDock_, 1);

    auto* dataForm = new QFormLayout();
    dataForm->setContentsMargins(6, 4, 6, 2);
    levelCombo_ = new QComboBox(dataPanel);
    connect(levelCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this,
            &MainWindow::onLevelChanged);
    dataForm->addRow(tr("Level"), levelCombo_);

    derivedCombo_ = new QComboBox(dataPanel);
    derivedCombo_->addItems({tr("(raw field)"), tr("Wind speed"), tr("Wind direction"),
                             tr("Rel. vorticity"), tr("Divergence"), tr("Potential temp θ")});
    derivedCombo_->setToolTip(tr("Compute a quantity from the current variable — e.g. θ from\n"
                                 "temperature, or wind speed/vorticity from the U/V pair."));
    connect(derivedCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this,
            &MainWindow::onDerivedChanged);
    dataForm->addRow(tr("Derived"), derivedCombo_);
    dataLayout->addLayout(dataForm);

    showingLabel_ = new QLabel(tr("No field loaded"), dataPanel);
    showingLabel_->setWordWrap(true);
    showingLabel_->setContentsMargins(6, 0, 6, 6);
    dataLayout->addWidget(showingLabel_);

    auto* leftDock = new QDockWidget(tr("Data"), this);
    leftDock->setObjectName("dataDock");
    leftDock->setWidget(dataPanel);
    addDockWidget(Qt::LeftDockWidgetArea, leftDock);

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
            if (mode != MapView::Mode::Pan) tabs_->setCurrentWidget(mapFrame_);
            // Tell the user what this picking mode expects (the actions aren't
            // otherwise discoverable, e.g. double-click to finish a section).
            QString hint;
            switch (mode) {
                case MapView::Mode::CrossSection:
                    hint = tr("Cross-section: click points along the path, then double-click to draw it.");
                    break;
                case MapView::Mode::Sounding:
                    hint = tr("Sounding: click a point on the map to plot its skew-T.");
                    break;
                case MapView::Mode::TimeSeries:
                    hint = tr("Time series: click a point on the map to plot its values over time.");
                    break;
                case MapView::Mode::Pan:
                    break;  // default mode: keep the status bar clear for the probe readout
            }
            if (hint.isEmpty()) statusBar()->clearMessage();
            else statusBar()->showMessage(hint);
        });
        if (m.mode == MapView::Mode::Pan) act->setChecked(true);
    }

    // View menu: the dock panels and the toolbar can be hidden via their close
    // button, so offer a way to bring them back. toggleViewAction() is a checkable
    // action auto-labeled with the panel's title.
    auto* viewMenu = menuBar()->addMenu(tr("&View"));
    viewMenu->addAction(leftDock->toggleViewAction());
    viewMenu->addAction(bottomDock->toggleViewAction());
    viewMenu->addSeparator();
    viewMenu->addAction(toolbar->toggleViewAction());

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
    refreshAnalyses();  // sections/soundings re-extract; time-series marker moves
}

void MainWindow::refreshAnalyses() {
    // Refresh each open analysis tab at the current time; prune closed ones.
    for (auto it = analyses_.begin(); it != analyses_.end();) {
        if (!it->frame) {
            it = analyses_.erase(it);
        } else {
            if (it->refresh) it->refresh();
            ++it;
        }
    }
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
        // Potential temperature — only when the current field is actually
        // temperature (converted to Kelvin), not any arbitrary variable.
        const auto& lvl = currentRaw_->meta.level;
        if (lvl.type == core::VerticalLevel::Type::PressureHPa) {
            if (auto tk = analysis::asTemperatureKelvin(*currentRaw_))
                derived = std::make_shared<core::Field2D>(
                    analysis::potentialTemperatureField(*tk, lvl.value));
        }
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
    // Signed fields read best on a diverging map (which the views auto-center at
    // zero); switch both field views to one unless a diverging map is already set.
    const bool signedField = (index == 3 || index == 4);
    if (signedField) {
        for (QComboBox* c : {plotColormapCombo_, mapColormapCombo_})
            if (c && !render::Colormap::isDiverging(c->currentText().toStdString()))
                c->setCurrentText("RdBu (diverging)");
    }
    presentField();
}

void MainWindow::displayField(std::shared_ptr<core::Field2D> field) {
    currentUnits_ = QString::fromStdString(field->meta.units);
    plot_->setField(field);    // each view auto-ranges and updates its own legend
    mapView_->setField(field);
    probeLabel_->setText(tr("%1 @ %2 — %3×%4")
                             .arg(QString::fromStdString(field->meta.varName))
                             .arg(QString::fromStdString(met::core::formatLevel(field->meta.level)))
                             .arg(field->width())
                             .arg(field->height()));
    updateShowingLabel();
    updateWind();
}

void MainWindow::updateShowingLabel() {
    if (!showingLabel_) return;
    if (!currentRaw_) {
        showingLabel_->setText(tr("No field loaded"));
        return;
    }
    const auto& meta = currentRaw_->meta;
    QString quantity;
    switch (derivedMode_) {
        case 1: quantity = tr("Wind speed"); break;
        case 2: quantity = tr("Wind direction"); break;
        case 3: quantity = tr("Relative vorticity"); break;
        case 4: quantity = tr("Divergence"); break;
        case 5: quantity = tr("Potential temperature θ"); break;
        default:
            quantity = QString::fromStdString(meta.longName.empty() ? meta.varName : meta.longName);
            break;
    }
    const QString from =
        derivedMode_ != 0 ? tr(" (from %1)").arg(QString::fromStdString(meta.varName)) : QString();
    showingLabel_->setText(tr("Showing: %1%2\n@ %3 · %4")
                               .arg(quantity, from,
                                    QString::fromStdString(met::core::formatLevel(meta.level)),
                                    QString::fromStdString(met::core::formatTime(meta.validTime))));
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
    readers::IDataset& ds, const std::string& varName, core::TimePoint time, int member) {
    std::vector<std::pair<double, core::Field2D>> stack;
    const auto* entry = ds.catalog().find(varName);
    if (!entry) return stack;
    for (const auto& lvl : entry->levels) {
        if (lvl.type != core::VerticalLevel::Type::PressureHPa) continue;
        try {
            stack.emplace_back(lvl.value, ds.readField(core::FieldKey{varName, lvl, time, member}));
        } catch (const std::exception&) {
        }
    }
    return stack;
}

std::vector<std::pair<core::TimePoint, core::Field2D>> MainWindow::readTimeStack(
    readers::IDataset& ds, const std::string& varName, core::VerticalLevel level, int member) {
    std::vector<std::pair<core::TimePoint, core::Field2D>> stack;
    const auto* entry = ds.catalog().find(varName);
    if (!entry) return stack;
    for (const auto& t : entry->times) {
        try {
            stack.emplace_back(t, ds.readField(core::FieldKey{varName, level, t, member}));
        } catch (const std::exception&) {
        }
    }
    return stack;
}

ViewFrame* MainWindow::buildPlotFrame() {
    auto* panel = new ControlPanel(tr("2D Plot"));
    plotColormapCombo_ = addColormapControls(panel, plot_);

    plotContourCheck_ = new QCheckBox(tr("Contours"), panel);
    plotContourCheck_->setToolTip(tr("Overlay contour lines on the 2D plot."));
    connect(plotContourCheck_, &QCheckBox::toggled, plot_, &PlotView2D::setContoursEnabled);
    panel->addRow(plotContourCheck_);

    auto* interval = new QDoubleSpinBox(panel);
    interval->setRange(0.0, 1e6);
    interval->setDecimals(3);
    interval->setSpecialValueText(tr("auto"));
    interval->setToolTip(tr("Spacing between contour lines (field units); \"auto\" picks a round value."));
    connect(interval, qOverload<double>(&QDoubleSpinBox::valueChanged), plot_,
            &PlotView2D::setContourInterval);
    panel->addRow(tr("Contour interval"), interval);

    plotWindCombo_ = new QComboBox(panel);
    plotWindCombo_->addItems({tr("Off"), tr("Barbs")});
    connect(plotWindCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int m) {
        plot_->setWindMode(m);
        updateWind();
    });
    panel->addRow(tr("Wind"), plotWindCombo_);

    return new ViewFrame(plot_, panel);
}

ViewFrame* MainWindow::buildMapFrame() {
    auto* panel = new ControlPanel(tr("Map"));
    mapColormapCombo_ = addColormapControls(panel, mapView_);

    mapBasemapCombo_ = new QComboBox(panel);
    for (const auto& src : TileLayer::builtinSources()) mapBasemapCombo_->addItem(src.name);
    connect(mapBasemapCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int index) {
                const auto sources = TileLayer::builtinSources();
                if (index < 0 || index >= sources.size()) return;
                tileLayer_->setSource(sources.at(index));
                mapView_->refreshSource();
            });
    panel->addRow(tr("Basemap"), mapBasemapCombo_);

    mapOpacitySlider_ = new QSlider(Qt::Horizontal, panel);
    mapOpacitySlider_->setRange(0, 100);
    mapOpacitySlider_->setValue(75);
    connect(mapOpacitySlider_, &QSlider::valueChanged, mapView_,
            [this](int p) { mapView_->setOpacity(p / 100.0); });
    panel->addRow(tr("Field opacity"), mapOpacitySlider_);

    mapGraticuleCheck_ = new QCheckBox(tr("Graticule"), panel);
    mapGraticuleCheck_->setChecked(true);
    connect(mapGraticuleCheck_, &QCheckBox::toggled, mapView_, &MapView::setGraticuleVisible);
    panel->addRow(mapGraticuleCheck_);

    mapCoastlineCheck_ = new QCheckBox(tr("Coastlines"), panel);
    mapCoastlineCheck_->setChecked(true);
    connect(mapCoastlineCheck_, &QCheckBox::toggled, mapView_, &MapView::setCoastlinesVisible);
    panel->addRow(mapCoastlineCheck_);

    mapGpuCheck_ = new QCheckBox(tr("GPU render (experimental)"), panel);
    connect(mapGpuCheck_, &QCheckBox::toggled, mapView_, &MapView::setGpuEnabled);
    panel->addRow(mapGpuCheck_);

    mapWindCombo_ = new QComboBox(panel);
    mapWindCombo_->addItems({tr("Off"), tr("Barbs"), tr("Streamlines")});
    connect(mapWindCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int m) {
        mapView_->setWindMode(m);
        updateWind();
    });
    panel->addRow(tr("Wind"), mapWindCombo_);

    return new ViewFrame(mapView_, panel);
}

ViewFrame* MainWindow::wrapCrossSection(CrossSectionView* view) {
    auto* panel = new ControlPanel(tr("Cross-section"));
    addColormapControls(panel, view);  // colormap + range + legend, wired to the section
    return new ViewFrame(view, panel);
}

void MainWindow::onCrossSectionRequested(const std::vector<core::LatLon>& path) {
    if (currentVar_.empty() || !dataset_ || currentTimes_.empty()) return;
    const std::string var = currentVar_;
    const core::TimePoint time = currentTimes_[static_cast<std::size_t>(timeIdx_)];
    const int member = currentMember_;
    auto ds = dataset_;
    statusBar()->showMessage(tr("Extracting cross-section…"));
    submitCompute<analysis::CrossSection>(
        *pool_, this,
        [ds, var, time, member, path] {
            return analysis::extractCrossSection(MainWindow::readLevelStack(*ds, var, time, member),
                                                 path, 200);
        },
        [this, var, path](analysis::CrossSection cs) {
            if (cs.pressures.size() < 2) {
                statusBar()->showMessage(tr("Cross-section needs a multi-level variable"), 4000);
                return;
            }
            auto* view = new CrossSectionView;
            auto* frame = wrapCrossSection(view);  // panel + legend wired first
            view->setSection(cs);                  // emits rangeChanged -> fills the legend
            const int idx =
                tabs_->addTab(frame, tr("Section: %1").arg(QString::fromStdString(var)));
            tabs_->setCurrentIndex(idx);
            statusBar()->clearMessage();
            // Follow the time slider: re-extract this section at the current time.
            analyses_.push_back({frame, [this, v = QPointer<CrossSectionView>(view), var, path]() {
                if (!v || !dataset_ || currentTimes_.empty()) return;
                const core::TimePoint t = currentTimes_[static_cast<std::size_t>(timeIdx_)];
                const int mem = currentMember_;
                auto dset = dataset_;
                submitCompute<analysis::CrossSection>(
                    *pool_, this,
                    [dset, var, t, mem, path] {
                        return analysis::extractCrossSection(
                            MainWindow::readLevelStack(*dset, var, t, mem), path, 200);
                    },
                    [v](analysis::CrossSection ncs) {
                        if (v && ncs.pressures.size() >= 2) v->setSection(ncs);
                    });
            }});
        });
}

void MainWindow::onSoundingRequested(core::LatLon point) {
    if (!dataset_ || currentTimes_.empty()) return;
    const core::TimePoint time = currentTimes_[static_cast<std::size_t>(timeIdx_)];
    const int member = currentMember_;
    auto ds = dataset_;
    statusBar()->showMessage(tr("Extracting sounding…"));
    submitCompute<analysis::Sounding>(
        *pool_, this,
        [ds, time, member, point] {
            // Temperature is required; relative humidity is optional (for dewpoint).
            const auto tStack = MainWindow::readLevelStack(*ds, "t", time, member);
            if (tStack.size() < 2) return analysis::Sounding{};
            const auto rhStack = MainWindow::readLevelStack(*ds, "r", time, member);
            return analysis::extractSounding(tStack, rhStack, point);
        },
        [this, point](analysis::Sounding s) {
            if (s.levels.size() < 2) {
                statusBar()->showMessage(tr("Sounding needs multi-level temperature (t)"), 4000);
                return;
            }
            auto* view = new SkewTView;
            view->setSounding(s);
            const int idx = tabs_->addTab(view, tr("Skew-T"));
            tabs_->setCurrentIndex(idx);
            statusBar()->clearMessage();
            // Follow the time slider: re-extract this sounding at the current time.
            analyses_.push_back({view, [this, v = QPointer<SkewTView>(view), point]() {
                if (!v || !dataset_ || currentTimes_.empty()) return;
                const core::TimePoint t = currentTimes_[static_cast<std::size_t>(timeIdx_)];
                const int mem = currentMember_;
                auto dset = dataset_;
                submitCompute<analysis::Sounding>(
                    *pool_, this,
                    [dset, t, mem, point] {
                        const auto tStack = MainWindow::readLevelStack(*dset, "t", t, mem);
                        if (tStack.size() < 2) return analysis::Sounding{};
                        const auto rhStack = MainWindow::readLevelStack(*dset, "r", t, mem);
                        return analysis::extractSounding(tStack, rhStack, point);
                    },
                    [v](analysis::Sounding ns) {
                        if (v && ns.levels.size() >= 2) v->setSounding(ns);
                    });
            }});
        });
}

void MainWindow::onTimeSeriesRequested(core::LatLon point) {
    if (currentVar_.empty() || !dataset_ || currentLevels_.empty()) return;
    const std::string var = currentVar_;
    const core::VerticalLevel level = currentLevels_[static_cast<std::size_t>(levelIdx_)];
    const int member = currentMember_;
    auto ds = dataset_;
    statusBar()->showMessage(tr("Extracting time series…"));
    submitCompute<analysis::TimeSeries>(
        *pool_, this,
        [ds, var, level, member, point] {
            return analysis::extractTimeSeries(MainWindow::readTimeStack(*ds, var, level, member),
                                               point);
        },
        [this, var](analysis::TimeSeries ts) {
            if (ts.values.size() < 2) {
                statusBar()->showMessage(tr("Time series needs multiple time steps"), 4000);
                return;
            }
            auto* view = new TimeSeriesView;
            view->setSeries(ts, QString::fromStdString(var));
            view->setCurrentIndex(timeIdx_);
            const int idx =
                tabs_->addTab(view, tr("Series: %1").arg(QString::fromStdString(var)));
            tabs_->setCurrentIndex(idx);
            statusBar()->clearMessage();
            // The series spans all times; just move its marker with the slider.
            analyses_.push_back({view, [this, v = QPointer<TimeSeriesView>(view)]() {
                if (v) v->setCurrentIndex(timeIdx_);
            }});
        });
}

void MainWindow::demoCrossSection() {
    onCrossSectionRequested({{68.0, 4.0}, {58.0, 26.0}});
}
void MainWindow::demoSounding() { onSoundingRequested({64.0, 12.0}); }
void MainWindow::demoTimeSeries() { onTimeSeriesRequested({64.0, 12.0}); }

void MainWindow::onTabCloseRequested(int index) {
    QWidget* w = tabs_->widget(index);
    if (!w || w == plotFrame_ || w == mapFrame_) return;  // the base tabs are permanent
    for (auto it = analyses_.begin(); it != analyses_.end();)
        it = (it->frame == w) ? analyses_.erase(it) : it + 1;
    tabs_->removeTab(index);
    w->deleteLater();
}

void MainWindow::setContoursChecked(bool on) {
    if (plotContourCheck_) plotContourCheck_->setChecked(on);
}

void MainWindow::showMapTab() {
    if (tabs_) tabs_->setCurrentWidget(mapFrame_);
}

void MainWindow::setWindComboIndex(int index) {
    if (mapWindCombo_) mapWindCombo_->setCurrentIndex(index);
    if (plotWindCombo_) plotWindCombo_->setCurrentIndex(index > 0 ? 1 : 0);  // plot: off/barbs only
}

void MainWindow::startPlayback() { timeController_->play(); }

void MainWindow::setGpuChecked(bool on) {
    if (mapGpuCheck_) mapGpuCheck_->setChecked(on);
}

void MainWindow::setDerivedComboIndex(int index) {
    if (derivedCombo_) derivedCombo_->setCurrentIndex(index);
}

std::shared_ptr<analysis::WindField> MainWindow::buildWindField() {
    if (!dataset_ || currentLevels_.empty() || currentTimes_.empty()) return nullptr;
    std::vector<std::string> names;
    for (const auto& v : dataset_->catalog().variables()) names.push_back(v.varName);
    const auto pair = analysis::findWindPair(names);
    if (!pair) return nullptr;

    const core::VerticalLevel level = currentLevels_[static_cast<std::size_t>(levelIdx_)];
    const core::TimePoint time = currentTimes_[static_cast<std::size_t>(timeIdx_)];
    // Route U/V through the field cache so animation with wind/derived overlays
    // does not re-decode both components from disk on every frame.
    auto fetch = [&](const core::FieldKey& key) -> std::shared_ptr<core::Field2D> {
        if (auto c = fieldCache_.get(key)) return c;
        auto f = std::make_shared<core::Field2D>(dataset_->readField(key));
        fieldCache_.put(key, f);
        return f;
    };
    try {
        auto wind = std::make_shared<analysis::WindField>();
        wind->u = *fetch(core::FieldKey{pair->uName, level, time, currentMember_});
        wind->v = *fetch(core::FieldKey{pair->vName, level, time, currentMember_});
        analysis::rotateToEarthRelative(*wind);  // rotates the copy, not the cached field
        return wind;
    } catch (const std::exception&) {
        return nullptr;  // U/V may not exist at this level/time
    }
}

void MainWindow::updateWind() {
    const int plotMode = plotWindCombo_ ? plotWindCombo_->currentIndex() : 0;
    const int mapMode = mapWindCombo_ ? mapWindCombo_->currentIndex() : 0;
    if (plotMode == 0 && mapMode == 0) {
        mapView_->setWind(nullptr);
        plot_->setWind(nullptr);
        return;
    }
    auto wind = buildWindField();  // shared field; each view draws per its own mode
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
    for (QComboBox* c : {plotColormapCombo_, mapColormapCombo_}) {
        const int ci = c->findText(cmap);
        if (ci >= 0) c->setCurrentIndex(ci);
    }
    const int bi = s.value("basemap", 0).toInt();
    if (bi >= 0 && bi < mapBasemapCombo_->count()) mapBasemapCombo_->setCurrentIndex(bi);
    mapOpacitySlider_->setValue(s.value("opacity", 75).toInt());
    mapGraticuleCheck_->setChecked(s.value("graticule", true).toBool());
    mapCoastlineCheck_->setChecked(s.value("coastlines", true).toBool());

    cacheBudgetMB_ = s.value("cacheBudgetMB", 1024).toInt();
    fieldCache_.setBudgetBytes(static_cast<std::size_t>(cacheBudgetMB_) * 1024 * 1024);
    animationFps_ = s.value("animationFps", 6).toInt();
    timeController_->setFps(animationFps_);
}

void MainWindow::saveSettings() {
    QSettings s;
    s.setValue("geometry", saveGeometry());
    s.setValue("windowState", saveState());
    s.setValue("colormap", mapColormapCombo_->currentText());
    s.setValue("basemap", mapBasemapCombo_->currentIndex());
    s.setValue("opacity", mapOpacitySlider_->value());
    s.setValue("graticule", mapGraticuleCheck_->isChecked());
    s.setValue("coastlines", mapCoastlineCheck_->isChecked());
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
