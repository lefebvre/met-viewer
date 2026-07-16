#include "viewer/app/mainwindow.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <map>
#include <set>
#include <utility>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHash>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QProgressBar>
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
#include "viewer/app/icons.h"
#include "viewer/app/jobs.h"
#include "viewer/app/mapview.h"
#include "viewer/app/plotview2d.h"
#include "viewer/app/skewtview.h"
#include "viewer/app/tilelayer.h"
#include "viewer/app/theme.h"
#include "viewer/app/timecontroller.h"
#include "viewer/app/timeseriesview.h"
#include "viewer/core/timeaxis.h"
#include "viewer/core/units.h"
#include "viewer/readers/detect.h"
#include "viewer/readers/multidataset.h"

namespace met::app {
namespace {

// Size a combo to its widest item so its reported sizeHint reflects the width the
// control actually needs. The control panel is then given that width (see
// ViewFrame), so dropdown text/arrows are not clipped on high-DPI or large-font
// systems instead of the combo silently shrinking and eliding.
void sizeComboToContents(QComboBox* c) {
    c->setSizeAdjustPolicy(QComboBox::AdjustToContents);
}

// Add colormap + auto/manual range controls and a colorbar legend to `panel`,
// wired to `view` (which must expose setColormapByName/setAutoRange/setRange/
// colormap()/units() and a rangeChanged(double,double) signal). Returns the
// colormap combo so the caller can persist / drive it programmatically. Shared by
// the 2D-Plot, Map, and cross-section panels so their color controls behave alike.
template <typename View>
QComboBox* addColormapControls(ControlPanel* panel, View* view, IconThemer* icons) {
    auto* cmap = new QComboBox(panel);
    for (const auto& name : render::Colormap::builtinNames())
        cmap->addItem(QString::fromStdString(name));
    cmap->setCurrentText(QString::fromStdString(view->colormap().name()));
    sizeComboToContents(cmap);

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

    panel->addRow(icons->iconLabel("render-cmap", 20, QObject::tr("Colormap")), cmap);
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
    // Apply the saved theme before building widgets so they start correctly styled.
    theme_ = new ThemeManager(this);
    icons_ = new IconThemer(theme_, this);
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

    // The center is a nested QMainWindow whose views are dock widgets, so the user
    // can drag a view's tab to split the area horizontally/vertically, tab views
    // together, or float them out (IDE-style). Base views are non-closable (they
    // simply omit the Closable feature); analysis views are closable.
    viewArea_ = new QMainWindow(this);
    viewArea_->setWindowFlags(Qt::Widget);  // act as a child widget, not a window
    viewArea_->setDockNestingEnabled(true);
    viewArea_->setDockOptions(QMainWindow::AllowNestedDocks | QMainWindow::AllowTabbedDocks |
                              QMainWindow::GroupedDragging);
    setCentralWidget(viewArea_);

    plotFrame_ = buildPlotFrame();
    mapFrame_ = buildMapFrame();
    plotDock_ = new QDockWidget(tr("2D Plot"), viewArea_);
    plotDock_->setObjectName("plotDock");
    plotDock_->setWidget(plotFrame_);
    plotDock_->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    mapDock_ = new QDockWidget(tr("Map"), viewArea_);
    mapDock_->setObjectName("mapDock");
    mapDock_->setWidget(mapFrame_);
    mapDock_->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    viewArea_->addDockWidget(Qt::LeftDockWidgetArea, plotDock_);
    viewArea_->addDockWidget(Qt::LeftDockWidgetArea, mapDock_);
    viewArea_->tabifyDockWidget(plotDock_, mapDock_);  // start tabbed; drag a tab to split
    plotDock_->raise();
    // Tab icons for the base views (a tabified dock shows its windowIcon).
    icons_->applyWindowIcon(plotDock_, "view-plot2d");
    icons_->applyWindowIcon(mapDock_, "view-map");

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
    sizeComboToContents(levelCombo_);
    connect(levelCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this,
            &MainWindow::onLevelChanged);
    dataForm->addRow(icons_->iconLabel("axis-level", 20, tr("Level")), levelCombo_);

    derivedCombo_ = new QComboBox(dataPanel);
    derivedCombo_->addItems({tr("(raw field)"), tr("Wind speed"), tr("Wind direction"),
                             tr("Rel. vorticity"), tr("Divergence"), tr("Potential temp θ")});
    sizeComboToContents(derivedCombo_);
    derivedCombo_->setToolTip(tr("Compute a quantity from the current variable — e.g. θ from\n"
                                 "temperature, or wind speed/vorticity from the U/V pair."));
    connect(derivedCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this,
            &MainWindow::onDerivedChanged);
    dataForm->addRow(icons_->iconLabel("data-grid", 20, tr("Derived")), derivedCombo_);
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
    timeController_->setIcons(icons_);
    auto* bottomDock = new QDockWidget(tr("Time"), this);
    bottomDock->setObjectName("timeDock");
    bottomDock->setWidget(timeController_);
    addDockWidget(Qt::BottomDockWidgetArea, bottomDock);
    connect(timeController_, &TimeController::indexChanged, this, &MainWindow::onTimeChanged);

    // Menu.
    auto* fileMenu = menuBar()->addMenu(tr("&File"));
    QAction* openAct = fileMenu->addAction(tr("&Open Files…"));
    openAct->setShortcut(QKeySequence::Open);
    icons_->applyAction(openAct, "file-open");
    connect(openAct, &QAction::triggered, this, &MainWindow::onOpenTriggered);
    QAction* openFolderAct = fileMenu->addAction(tr("Open &Folder…"));
    icons_->applyAction(openFolderAct, "file-open");
    connect(openFolderAct, &QAction::triggered, this, &MainWindow::onOpenFolderTriggered);
    QAction* addFilesAct = fileMenu->addAction(tr("&Add Files…"));
    connect(addFilesAct, &QAction::triggered, this, &MainWindow::onAddFilesTriggered);
    recentMenu_ = fileMenu->addMenu(tr("Open &Recent"));
    recentMenu_->setToolTipsVisible(true);  // show full paths on hover
    updateRecentMenu();
    QAction* prefAct = fileMenu->addAction(tr("&Preferences…"));
    icons_->applyAction(prefAct, "app-settings");
    connect(prefAct, &QAction::triggered, this, &MainWindow::openPreferences);
    fileMenu->addSeparator();
    QAction* quitAct = fileMenu->addAction(tr("&Quit"));
    quitAct->setShortcut(QKeySequence::Quit);
    connect(quitAct, &QAction::triggered, this, &QWidget::close);

    // Interaction-mode toolbar (map picking).
    auto* toolbar = addToolBar(tr("Tools"));
    toolbar->setObjectName("toolsToolbar");
    toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);  // icons + tooltips
    auto* modeGroup = new QActionGroup(this);
    modeGroup->setExclusive(true);
    struct ModeDef { const char* label; MapView::Mode mode; const char* icon; };
    const ModeDef modes[] = {{"Pan", MapView::Mode::Pan, "view-pan"},
                             {"Cross-section", MapView::Mode::CrossSection, "mode-section"},
                             {"Sounding", MapView::Mode::Sounding, "mode-skewt"},
                             {"Time series", MapView::Mode::TimeSeries, "mode-tseries"}};
    for (const auto& m : modes) {
        QAction* act = toolbar->addAction(tr(m.label));
        act->setCheckable(true);
        icons_->applyAction(act, m.icon);
        modeGroup->addAction(act);
        const MapView::Mode mode = m.mode;
        connect(act, &QAction::triggered, this, [this, mode]() {
            mapView_->setInteractionMode(mode);
            if (mode != MapView::Mode::Pan) {
                mapDock_->show();
                mapDock_->raise();
            }
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
    QAction* dataToggle = leftDock->toggleViewAction();
    icons_->applyAction(dataToggle, "view-layers");
    viewMenu->addAction(dataToggle);
    QAction* timeToggle = bottomDock->toggleViewAction();
    icons_->applyAction(timeToggle, "axis-time");
    viewMenu->addAction(timeToggle);
    viewMenu->addSeparator();
    viewMenu->addAction(toolbar->toggleViewAction());

    // Theme: System (follow OS light/dark) / Light / Dark, persisted by ThemeManager.
    viewMenu->addSeparator();
    auto* themeMenu = viewMenu->addMenu(tr("&Theme"));
    auto* themeGroup = new QActionGroup(this);
    const struct { const char* label; ThemeManager::Mode mode; } themes[] = {
        {QT_TR_NOOP("&System"), ThemeManager::Mode::System},
        {QT_TR_NOOP("&Light"), ThemeManager::Mode::Light},
        {QT_TR_NOOP("&Dark"), ThemeManager::Mode::Dark},
    };
    for (const auto& t : themes) {
        QAction* act = themeMenu->addAction(tr(t.label));
        act->setCheckable(true);
        act->setChecked(theme_->mode() == t.mode);
        themeGroup->addAction(act);
        const ThemeManager::Mode mode = t.mode;
        connect(act, &QAction::triggered, this, [this, mode]() { theme_->setMode(mode); });
    }

    probeLabel_ = new QLabel(tr("Ready"), this);
    statusBar()->addWidget(probeLabel_);

    // Background-job progress bar, parked on the right of the status bar and hidden
    // until a job runs. A single shared bar aggregates all in-flight jobs.
    progressBar_ = new QProgressBar(this);
    progressBar_->setMaximumWidth(180);
    progressBar_->setMaximumHeight(14);
    progressBar_->setTextVisible(true);
    progressBar_->hide();
    statusBar()->addPermanentWidget(progressBar_);
    progressTimer_ = new QTimer(this);
    progressTimer_->setInterval(100);
    connect(progressTimer_, &QTimer::timeout, this, &MainWindow::pollProgress);
}

void MainWindow::beginJob(const QString& text, std::shared_ptr<JobProgress> progress) {
    activeJobs_.push_back(std::move(progress));
    if (!text.isEmpty()) statusBar()->showMessage(text);
    progressBar_->show();
    if (!progressTimer_->isActive()) progressTimer_->start();
    pollProgress();
}

void MainWindow::endJob(const std::shared_ptr<JobProgress>& progress) {
    std::erase(activeJobs_, progress);
    if (activeJobs_.empty()) {
        progressTimer_->stop();
        progressBar_->hide();
        statusBar()->clearMessage();
    } else {
        pollProgress();
    }
}

void MainWindow::pollProgress() {
    if (activeJobs_.empty()) return;
    long long done = 0, total = 0;
    bool anyBusy = false;
    for (const auto& j : activeJobs_) {
        const int t = j->total.load(std::memory_order_relaxed);
        // total 0 = opaque decode; generating = slabs loaded, now extracting/rendering.
        if (t <= 0 || j->generating.load(std::memory_order_relaxed)) { anyBusy = true; continue; }
        total += t;
        done += std::min(j->done.load(std::memory_order_relaxed), t);
    }
    if (anyBusy || total <= 0) {
        progressBar_->setRange(0, 0);  // indeterminate / busy
    } else {
        progressBar_->setRange(0, static_cast<int>(total));
        progressBar_->setValue(static_cast<int>(done));
    }
}

void MainWindow::scheduleGrabAndQuit(const QString& pngPath, int delayMs) {
    QTimer::singleShot(delayMs, this, [this, pngPath]() {
        grab().save(pngPath);
        QApplication::quit();
    });
}

// The Open/Add file-dialog filter (GRIB editions, NetCDF).
static QString dataFileFilter() {
    return QObject::tr(
        "Meteorological data (*.grib *.grib2 *.grb *.grb2 *.grib1 *.nc *.nc4);;All files (*)");
}

void MainWindow::onOpenTriggered() {
    const QStringList paths = QFileDialog::getOpenFileNames(
        this, tr("Open meteorological files"), {}, dataFileFilter());
    if (!paths.isEmpty()) openFiles(paths, /*replace=*/true);
}

void MainWindow::onAddFilesTriggered() {
    const QStringList paths = QFileDialog::getOpenFileNames(
        this, tr("Add meteorological files to the current set"), {}, dataFileFilter());
    if (!paths.isEmpty()) openFiles(paths, /*replace=*/false);
}

void MainWindow::onOpenFolderTriggered() {
    const QString folder =
        QFileDialog::getExistingDirectory(this, tr("Open a folder of meteorological files"));
    if (folder.isEmpty()) return;
    // Pick recognized data files by extension (ARL has no standard extension, so it
    // isn't matched here — use Open Files for those).
    static const QStringList kNameFilters = {"*.grib", "*.grib2", "*.grb", "*.grb2",
                                             "*.grib1", "*.nc", "*.nc4"};
    QDir dir(folder);
    QStringList paths;
    for (const QString& name : dir.entryList(kNameFilters, QDir::Files, QDir::Name))
        paths << dir.filePath(name);
    if (paths.isEmpty()) {
        QMessageBox::information(
            this, tr("No data files"),
            tr("No recognized data files (*.grib2, *.nc, …) were found in:\n%1").arg(folder));
        return;
    }
    openFiles(paths, /*replace=*/true);
}

void MainWindow::openFile(const QString& path) { openFiles({path}, /*replace=*/true); }

std::vector<std::filesystem::path> MainWindow::pathsToOpen(const QStringList& paths,
                                                           bool replace) const {
    // For an add, skip files already in the set. De-dupe the request and sort so the
    // load order is deterministic (HRRR hourly names sort chronologically).
    std::set<std::filesystem::path> existing;
    if (!replace)
        for (const auto& p : loadedPaths_) existing.insert(p);

    std::set<std::filesystem::path> seen;
    std::vector<std::filesystem::path> out;
    for (const QString& p : paths) {
        if (p.isEmpty()) continue;
        std::filesystem::path fp(p.toStdString());
        if (existing.count(fp) || !seen.insert(fp).second) continue;
        out.push_back(std::move(fp));
    }
    std::sort(out.begin(), out.end());
    return out;
}

MainWindow::OpenBatch MainWindow::openBatch(const std::vector<std::filesystem::path>& paths,
                                            JobProgress* progress) {
    OpenBatch batch;
    for (const auto& path : paths) {
        try {
            batch.opened.emplace_back(
                path, std::shared_ptr<readers::IDataset>(readers::openDataset(path)));
        } catch (const std::exception& e) {
            // Keep the reason (e.g. "no reader recognizes file") so the skipped list
            // tells the user why, not just which.
            batch.skipped << (QString::fromStdString(path.filename().string()) + ": " +
                              QString::fromUtf8(e.what()));
        }
        if (progress) progress->done.fetch_add(1, std::memory_order_relaxed);
    }
    return batch;
}

void MainWindow::openFiles(const QStringList& paths, bool replace) {
    const std::vector<std::filesystem::path> newPaths = pathsToOpen(paths, replace);
    if (newPaths.empty()) return;

    // Scan the files on the thread pool with a determinate progress bar so a
    // multi-file open (a full HRRR day is ~23 × ~0.6 s) never freezes the UI.
    const quint64 gen = ++openGeneration_;
    auto progress = std::make_shared<JobProgress>();
    progress->total.store(static_cast<int>(newPaths.size()), std::memory_order_relaxed);
    beginJob(tr("Opening %n file(s)…", nullptr, static_cast<int>(newPaths.size())), progress);

    submitCompute<OpenBatch>(
        *pool_, this,
        [newPaths, progress]() { return openBatch(newPaths, progress.get()); },
        [this, gen, replace, progress](OpenBatch batch) {
            endJob(progress);
            if (gen != openGeneration_) return;  // a newer open superseded this one
            installBatch(std::move(batch), replace);
        });
}

void MainWindow::openFilesBlocking(const QStringList& paths) {
    const std::vector<std::filesystem::path> newPaths = pathsToOpen(paths, /*replace=*/true);
    if (newPaths.empty()) return;
    ++openGeneration_;  // supersede any in-flight async open
    installBatch(openBatch(newPaths, nullptr), /*replace=*/true);
}

void MainWindow::installBatch(OpenBatch batch, bool replace) {
    if (batch.opened.empty()) {
        // Nothing opened (all failed / none selected): keep any existing dataset and
        // loaded set intact and just report the failures. A failed "replace" must not
        // clobber the data already on screen.
        if (!batch.skipped.isEmpty())
            QMessageBox::warning(this, tr("Open failed"),
                                 tr("Could not open:\n%1").arg(batch.skipped.join(QChar('\n'))));
        return;
    }

    if (replace) {
        loadedPaths_.clear();
        loadedSources_.clear();
        fieldCache_.clear();  // a fresh set invalidates cached fields
        ++datasetEpoch_;      // and invalidates the per-tab analysis caches
    }
    for (auto& [path, ds] : batch.opened) {
        loadedPaths_.push_back(path);
        loadedSources_.push_back(std::move(ds));
    }

    // Remember the current field so an "add" keeps showing it after the (now larger)
    // time axis merges in. Captured before onFieldChosen() overwrites the state.
    core::FieldKey keep;
    const bool keepSelection =
        !replace && !currentVar_.empty() && levelIdx_ >= 0 &&
        levelIdx_ < static_cast<int>(currentLevels_.size()) && timeIdx_ >= 0 &&
        timeIdx_ < static_cast<int>(currentTimes_.size());
    if (keepSelection) {
        keep.varName = currentVar_;
        keep.level = currentLevels_[static_cast<std::size_t>(levelIdx_)];
        keep.validTime = currentTimes_[static_cast<std::size_t>(timeIdx_)];
        keep.member = currentMember_;
    }

    dataset_ = loadedSources_.size() == 1
                   ? loadedSources_.front()
                   : std::make_shared<readers::MultiDataset>(loadedSources_);
    datasetDock_->setCatalog(dataset_->catalog());
    if (replace) plot_->clearField();

    statusBar()->showMessage(
        tr("Opened %n file(s) (%1)", nullptr, static_cast<int>(loadedSources_.size()))
            .arg(QString::fromStdString(dataset_->formatName())),
        4000);

    if (!batch.skipped.isEmpty())
        QMessageBox::warning(this, tr("Some files skipped"),
                             tr("Could not open:\n%1").arg(batch.skipped.join(QChar('\n'))));

    // Recent files stays a pool of individually re-openable files; only record when a
    // single file was opened so a folder/multi-open doesn't flood the 10-entry list.
    if (batch.opened.size() == 1)
        addRecentFile(QString::fromStdString(batch.opened.front().first.string()));

    // Restore the prior field (add) or show the first available one (replace/no prior).
    if (keepSelection && dataset_->catalog().find(keep.varName)) {
        onFieldChosen(keep);
        return;
    }
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
    // Kick off the analyses first so their in-flight state is set before the field's
    // (possibly synchronous, cache-hit) settle check decides whether to advance.
    refreshAnalyses();  // sections/soundings re-extract; time-series marker moves
    decodeCurrent();
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
    fieldReadyForStep_ = false;  // a (re)decode: the field is not ready until it settles
    if (!dataset_ || currentVar_.empty() || currentLevels_.empty() || currentTimes_.empty()) {
        // Nothing to decode. Keep playback alive across a transient empty var/level,
        // but stop it outright if there is genuinely no data to animate (else the
        // closed loop would spin at the frame rate doing nothing).
        if (!dataset_ || currentTimes_.empty()) {
            if (timeController_) timeController_->pause();
        } else {
            fieldReadyForStep_ = true;
            maybeAdvancePlayback();
        }
        return;
    }

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
        fieldReadyForStep_ = true;
        maybeAdvancePlayback();  // field ready → advance if analyses are ready too
        return;
    }

    probeLabel_->setText(tr("Decoding…"));
    // A single opaque decode has no sub-steps: total 0 => the bar shows a busy
    // animation (empty text keeps probeLabel_'s "Decoding…" as the message).
    auto prog = std::make_shared<JobProgress>();
    beginJob(QString(), prog);
    submitDecode(*pool_, dataset_, key, gen, this, [this, gen, key, prog](DecodeOutcome outcome) {
        endJob(prog);
        if (!outcome.field) {
            if (outcome.generation == generation_) {
                probeLabel_->setText(tr("Decode error: %1").arg(outcome.error));
                fieldReadyForStep_ = true;  // treat a bad frame as "settled" so playback continues
                maybeAdvancePlayback();
            }
            return;
        }
        fieldCache_.put(key, outcome.field);
        if (outcome.generation != generation_) return;  // superseded; kept in cache
        currentRaw_ = outcome.field;
        presentField();
        prefetchAhead();
        fieldReadyForStep_ = true;
        maybeAdvancePlayback();  // field ready → advance if analyses are ready too
    });
}

void MainWindow::maybeAdvancePlayback() {
    // Closed-loop gate: advance only while playing, once the field has settled AND no
    // open analysis (cross-section/skew-T) is still extracting. Whichever load
    // finishes last fires the advance. A no-op when not playing, so it is safe on
    // every decodeCurrent / analysis-completion path.
    if (!timeController_ || !timeController_->isPlaying()) return;
    if (!fieldReadyForStep_) return;
    for (const auto& a : analyses_)
        if (a.frame && a.loading && a.loading()) return;  // an analysis is still loading
    timeController_->frameReady();
}

void MainWindow::presentField() {
    if (!currentRaw_) return;
    if (derivedMode_ == 0) {
        showingDerived_ = false;
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
        showingDerived_ = true;
        displayField(derived);
    } else {
        showingDerived_ = false;
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
    switch (showingDerived_ ? derivedMode_ : 0) {  // reflect what is actually on screen
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
        showingDerived_ ? tr(" (from %1)").arg(QString::fromStdString(meta.varName)) : QString();
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

namespace {
// Count a variable's levels of the given kind (pressure vs native model), for
// sizing the progress bar without any I/O.
int countPressureLevels(const readers::IDataset& ds, const std::string& var) {
    const auto* entry = ds.catalog().find(var);
    if (!entry) return 0;
    int n = 0;
    for (const auto& lvl : entry->levels)
        if (lvl.type == core::VerticalLevel::Type::PressureHPa) ++n;
    return n;
}
int countModelLevels(const readers::IDataset& ds, const std::string& var) {
    const auto* entry = ds.catalog().find(var);
    if (!entry) return 0;
    int n = 0;
    for (const auto& lvl : entry->levels)
        if (lvl.type == core::VerticalLevel::Type::Hybrid ||
            lvl.type == core::VerticalLevel::Type::Sigma)
            ++n;
    return n;
}
}  // namespace

std::vector<std::pair<double, core::Field2D>> MainWindow::readLevelStack(
    readers::IDataset& ds, const std::string& varName, core::TimePoint time, int member,
    const std::function<void()>& onRead) {
    std::vector<std::pair<double, core::Field2D>> stack;
    const auto* entry = ds.catalog().find(varName);
    if (!entry) return stack;
    for (const auto& lvl : entry->levels) {
        if (lvl.type != core::VerticalLevel::Type::PressureHPa) continue;
        try {
            stack.emplace_back(lvl.value, ds.readField(core::FieldKey{varName, lvl, time, member}));
        } catch (const std::exception&) {
        }
        if (onRead) onRead();
    }
    return stack;
}

std::vector<std::pair<double, core::Field2D>> MainWindow::readModelLevelStack(
    readers::IDataset& ds, const std::string& varName, core::TimePoint time, int member,
    const std::function<void()>& onRead) {
    std::vector<std::pair<double, core::Field2D>> stack;
    const auto* entry = ds.catalog().find(varName);
    if (!entry) return stack;
    for (const auto& lvl : entry->levels) {
        if (lvl.type != core::VerticalLevel::Type::Hybrid &&
            lvl.type != core::VerticalLevel::Type::Sigma)
            continue;
        try {
            stack.emplace_back(lvl.value, ds.readField(core::FieldKey{varName, lvl, time, member}));
        } catch (const std::exception&) {
        }
        if (onRead) onRead();
    }
    return stack;
}

std::vector<std::pair<core::TimePoint, core::Field2D>> MainWindow::readTimeStack(
    readers::IDataset& ds, const std::string& varName, core::VerticalLevel level, int member,
    const std::function<void()>& onRead) {
    std::vector<std::pair<core::TimePoint, core::Field2D>> stack;
    const auto* entry = ds.catalog().find(varName);
    if (!entry) return stack;
    for (const auto& t : entry->times) {
        try {
            stack.emplace_back(t, ds.readField(core::FieldKey{varName, level, t, member}));
        } catch (const std::exception&) {
        }
        if (onRead) onRead();
    }
    return stack;
}

void MainWindow::readWindStacks(readers::IDataset& ds, core::TimePoint time, int member,
                                std::vector<std::pair<double, core::Field2D>>& uStack,
                                std::vector<std::pair<double, core::Field2D>>& vStack,
                                bool modelLevels, const std::function<void()>& onRead) {
    uStack.clear();
    vStack.clear();
    std::vector<std::string> names;
    for (const auto& v : ds.catalog().variables()) names.push_back(v.varName);
    const auto pair = analysis::findWindPair(names);
    if (!pair) return;  // no U/V pair -> no wind profile
    uStack = modelLevels ? readModelLevelStack(ds, pair->uName, time, member, onRead)
                         : readLevelStack(ds, pair->uName, time, member, onRead);
    vStack = modelLevels ? readModelLevelStack(ds, pair->vName, time, member, onRead)
                         : readLevelStack(ds, pair->vName, time, member, onRead);
}

namespace {
// A per-slab tick bound to a job's counter (no-op when there is no job).
std::function<void()> readTick(const std::shared_ptr<JobProgress>& p) {
    if (!p) return {};
    return [p] { p->done.fetch_add(1, std::memory_order_relaxed); };
}
// Mark the transition from loading slabs to extracting/rendering the plot, so the
// bar shows a busy animation for that (unmeasured, worker-thread) phase.
void markGenerating(const std::shared_ptr<JobProgress>& p) {
    if (p) p->generating.store(true, std::memory_order_relaxed);
}
}  // namespace

analysis::Sounding MainWindow::computeSounding(readers::IDataset& ds, core::TimePoint time,
                                               int member, core::LatLon point,
                                               std::shared_ptr<JobProgress> progress) {
    const auto onRead = readTick(progress);
    // Isobaric path: temperature required; relative humidity (dewpoint) and U/V
    // (wind profile) optional.
    const auto tStack = readLevelStack(ds, "t", time, member, onRead);
    if (tStack.size() >= 2) {
        const auto rhStack = readLevelStack(ds, "r", time, member, onRead);
        std::vector<std::pair<double, core::Field2D>> uStack, vStack;
        readWindStacks(ds, time, member, uStack, vStack, /*modelLevels=*/false, onRead);
        markGenerating(progress);
        return analysis::extractSounding(tStack, rhStack, point, uStack, vStack);
    }
    // Native model-level path: pressure comes from the `pres` field, dewpoint from
    // specific humidity `q`.
    const auto tModel = readModelLevelStack(ds, "t", time, member, onRead);
    const auto presStack = readModelLevelStack(ds, "pres", time, member, onRead);
    if (tModel.size() < 2 || presStack.size() < 2) return analysis::Sounding{};
    const auto qStack = readModelLevelStack(ds, "q", time, member, onRead);
    std::vector<std::pair<double, core::Field2D>> uStack, vStack;
    readWindStacks(ds, time, member, uStack, vStack, /*modelLevels=*/true, onRead);
    markGenerating(progress);
    return analysis::extractSoundingModelLevels(tModel, presStack, qStack, point, uStack, vStack);
}

analysis::CrossSection MainWindow::computeCrossSection(readers::IDataset& ds, const std::string& var,
                                                       core::TimePoint time, int member,
                                                       const std::vector<core::LatLon>& path,
                                                       int nSamples,
                                                       std::shared_ptr<JobProgress> progress) {
    const auto onRead = readTick(progress);
    const auto pres = readLevelStack(ds, var, time, member, onRead);
    if (pres.size() >= 2) {
        markGenerating(progress);
        return analysis::extractCrossSection(pres, path, nSamples);
    }
    // Native model-level path with a terrain-following pressure axis.
    const auto model = readModelLevelStack(ds, var, time, member, onRead);
    const auto presStack = readModelLevelStack(ds, "pres", time, member, onRead);
    if (model.size() < 2 || presStack.size() < 2) return analysis::CrossSection{};
    markGenerating(progress);
    return analysis::extractCrossSectionModelLevels(model, presStack, path, nSamples);
}

int MainWindow::estimateSoundingReads(readers::IDataset& ds) {
    std::vector<std::string> names;
    for (const auto& v : ds.catalog().variables()) names.push_back(v.varName);
    const auto wind = analysis::findWindPair(names);
    const std::string uName = wind ? wind->uName : std::string{};
    const std::string vName = wind ? wind->vName : std::string{};
    if (countPressureLevels(ds, "t") >= 2) {  // isobaric path
        return countPressureLevels(ds, "t") + countPressureLevels(ds, "r") +
               countPressureLevels(ds, uName) + countPressureLevels(ds, vName);
    }
    return countModelLevels(ds, "t") + countModelLevels(ds, "pres") + countModelLevels(ds, "q") +
           countModelLevels(ds, uName) + countModelLevels(ds, vName);
}

int MainWindow::estimateCrossSectionReads(readers::IDataset& ds, const std::string& var) {
    const int pl = countPressureLevels(ds, var);
    if (pl >= 2) return pl;
    return countModelLevels(ds, var) + countModelLevels(ds, "pres");
}

ViewFrame* MainWindow::buildPlotFrame() {
    auto* panel = new ControlPanel(tr("2D Plot"));
    plotColormapCombo_ = addColormapControls(panel, plot_, icons_);

    plotContourCheck_ = new QCheckBox(panel);
    plotContourCheck_->setToolTip(tr("Overlay contour lines on the 2D plot."));
    plotContourCheck_->setAccessibleName(tr("Contours"));
    icons_->applyButton(plotContourCheck_, "render-contours");
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
    icons_->applyComboItem(plotWindCombo_, 1, "wind-barb");
    sizeComboToContents(plotWindCombo_);
    connect(plotWindCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int m) {
        plot_->setWindMode(m);
        updateWind();
    });
    panel->addRow(icons_->iconLabel("wind-barb", 20, tr("Wind")), plotWindCombo_);

    return new ViewFrame(plot_, panel);
}

ViewFrame* MainWindow::buildMapFrame() {
    auto* panel = new ControlPanel(tr("Map"));
    mapColormapCombo_ = addColormapControls(panel, mapView_, icons_);

    mapViewRangeCheck_ = new QCheckBox(tr("Range to view"), panel);
    mapViewRangeCheck_->setToolTip(
        tr("When auto-ranging, rescale the color range to the data currently visible in the map."));
    connect(mapViewRangeCheck_, &QCheckBox::toggled, mapView_, &MapView::setViewRange);
    panel->addRow(mapViewRangeCheck_);

    mapBasemapCombo_ = new QComboBox(panel);
    // Map each basemap source name to a glyph token where one fits.
    static const QHash<QString, QString> kBasemapIcons = {
        {"OpenStreetMap", "base-osm"},     {"Carto Light", "base-light"},
        {"Carto Dark", "base-dark"},       {"Esri World Imagery", "base-imagery"},
        {"OpenTopoMap", "base-terrain"},
    };
    for (const auto& src : TileLayer::builtinSources()) {
        mapBasemapCombo_->addItem(src.name);
        const QString token = kBasemapIcons.value(src.name, QStringLiteral("base-custom"));
        icons_->applyComboItem(mapBasemapCombo_, mapBasemapCombo_->count() - 1, token);
    }
    sizeComboToContents(mapBasemapCombo_);
    connect(mapBasemapCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int index) {
                const auto sources = TileLayer::builtinSources();
                if (index < 0 || index >= sources.size()) return;
                tileLayer_->setSource(sources.at(index));
                mapView_->refreshSource();
            });
    panel->addRow(icons_->iconLabel("base-osm", 20, tr("Basemap")), mapBasemapCombo_);

    mapOpacitySlider_ = new QSlider(Qt::Horizontal, panel);
    mapOpacitySlider_->setRange(0, 100);
    mapOpacitySlider_->setValue(75);
    connect(mapOpacitySlider_, &QSlider::valueChanged, mapView_,
            [this](int p) { mapView_->setOpacity(p / 100.0); });
    panel->addRow(icons_->iconLabel("layer-opacity", 20, tr("Field opacity")), mapOpacitySlider_);

    mapGraticuleCheck_ = new QCheckBox(panel);
    mapGraticuleCheck_->setChecked(true);
    mapGraticuleCheck_->setToolTip(tr("Graticule"));
    mapGraticuleCheck_->setAccessibleName(tr("Graticule"));
    icons_->applyButton(mapGraticuleCheck_, "overlay-graticule");
    connect(mapGraticuleCheck_, &QCheckBox::toggled, mapView_, &MapView::setGraticuleVisible);
    panel->addRow(mapGraticuleCheck_);

    mapCoastlineCheck_ = new QCheckBox(panel);
    mapCoastlineCheck_->setChecked(true);
    mapCoastlineCheck_->setToolTip(tr("Coastlines"));
    mapCoastlineCheck_->setAccessibleName(tr("Coastlines"));
    icons_->applyButton(mapCoastlineCheck_, "overlay-coast");
    connect(mapCoastlineCheck_, &QCheckBox::toggled, mapView_, &MapView::setCoastlinesVisible);
    panel->addRow(mapCoastlineCheck_);

    mapContourCheck_ = new QCheckBox(panel);
    mapContourCheck_->setToolTip(tr("Overlay contour lines on the map."));
    mapContourCheck_->setAccessibleName(tr("Contours"));
    icons_->applyButton(mapContourCheck_, "render-contours");
    connect(mapContourCheck_, &QCheckBox::toggled, mapView_, &MapView::setContoursEnabled);
    panel->addRow(mapContourCheck_);

    auto* mapInterval = new QDoubleSpinBox(panel);
    mapInterval->setRange(0.0, 1e6);
    mapInterval->setDecimals(3);
    mapInterval->setSpecialValueText(tr("auto"));
    mapInterval->setToolTip(tr("Spacing between contour lines (field units); \"auto\" picks a round value."));
    connect(mapInterval, qOverload<double>(&QDoubleSpinBox::valueChanged), mapView_,
            &MapView::setContourInterval);
    panel->addRow(tr("Contour interval"), mapInterval);

    mapGpuCheck_ = new QCheckBox(tr("GPU render (experimental)"), panel);
    connect(mapGpuCheck_, &QCheckBox::toggled, mapView_, &MapView::setGpuEnabled);
    panel->addRow(mapGpuCheck_);

    mapWindCombo_ = new QComboBox(panel);
    mapWindCombo_->addItems({tr("Off"), tr("Barbs"), tr("Streamlines")});
    icons_->applyComboItem(mapWindCombo_, 1, "wind-barb");
    icons_->applyComboItem(mapWindCombo_, 2, "wind-streamlines");
    sizeComboToContents(mapWindCombo_);
    connect(mapWindCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int m) {
        mapView_->setWindMode(m);
        updateWind();
    });
    panel->addRow(icons_->iconLabel("wind-barb", 20, tr("Wind")), mapWindCombo_);

    return new ViewFrame(mapView_, panel);
}

ViewFrame* MainWindow::wrapCrossSection(CrossSectionView* view) {
    auto* panel = new ControlPanel(tr("Cross-section"));
    addColormapControls(panel, view, icons_);  // colormap + range + legend, wired to the section
    return new ViewFrame(view, panel);
}

// Per-tab state backing a time-following cross-section / skew-T: an inFlight/pending
// pair that coalesces refreshes to one extraction at a time, plus a per-(validTime,
// member) cache of the extracted result so a looping Play recomputes nothing after the
// first pass. `epoch` records the datasetEpoch_ the cache was built against.
struct MainWindow::CrossSectionTab {
    bool inFlight = false;
    bool pending = false;
    quint64 epoch = 0;
    std::map<std::pair<std::int64_t, int>, analysis::CrossSection> cache;
};

struct MainWindow::SoundingTab {
    bool inFlight = false;
    bool pending = false;
    quint64 epoch = 0;
    std::map<std::pair<std::int64_t, int>, analysis::Sounding> cache;
};

void MainWindow::refreshCrossSectionTab(QPointer<CrossSectionView> view, std::string var,
                                        std::vector<core::LatLon> path,
                                        std::shared_ptr<CrossSectionTab> tab) {
    if (!view || !dataset_ || currentTimes_.empty()) return;
    if (tab->epoch != datasetEpoch_) {  // a dataset replace invalidated the cache
        tab->cache.clear();
        tab->epoch = datasetEpoch_;
    }
    const core::TimePoint t = currentTimes_[static_cast<std::size_t>(timeIdx_)];
    const int mem = currentMember_;
    const auto key = std::make_pair(static_cast<std::int64_t>(t.epochSeconds), mem);

    if (auto it = tab->cache.find(key); it != tab->cache.end()) {
        view->setSection(it->second);  // instant cache hit — no extraction thread
        return;
    }
    if (tab->inFlight) {  // an extraction is running; chase the latest time when it lands
        tab->pending = true;
        return;
    }
    tab->inFlight = true;
    auto dset = dataset_;
    auto p = std::make_shared<JobProgress>();
    p->total = estimateCrossSectionReads(*dset, var);
    beginJob(tr("Updating cross-section…"), p);
    submitCompute<analysis::CrossSection>(
        *pool_, this,
        [dset, var, t, mem, path, p] {
            return MainWindow::computeCrossSection(*dset, var, t, mem, path, 200, p);
        },
        [this, view, var, path, tab, key, p](analysis::CrossSection ncs) {
            endJob(p);
            tab->inFlight = false;
            if (ncs.pressures.size() >= 2) {
                tab->cache[key] = ncs;             // cache the result for this (time, member)
                if (view) view->setSection(ncs);   // always apply (coalescing keeps these in order)
            }
            if (tab->pending) {  // newer time requested while this ran → re-chase the current one
                tab->pending = false;
                refreshCrossSectionTab(view, var, path, tab);
            }
            maybeAdvancePlayback();  // this tab's load settled → maybe advance
        });
}

void MainWindow::refreshSoundingTab(QPointer<SkewTView> view, core::LatLon point,
                                    std::shared_ptr<SoundingTab> tab) {
    if (!view || !dataset_ || currentTimes_.empty()) return;
    if (tab->epoch != datasetEpoch_) {
        tab->cache.clear();
        tab->epoch = datasetEpoch_;
    }
    const core::TimePoint t = currentTimes_[static_cast<std::size_t>(timeIdx_)];
    const int mem = currentMember_;
    const auto key = std::make_pair(static_cast<std::int64_t>(t.epochSeconds), mem);

    if (auto it = tab->cache.find(key); it != tab->cache.end()) {
        view->setSounding(it->second);
        return;
    }
    if (tab->inFlight) {
        tab->pending = true;
        return;
    }
    tab->inFlight = true;
    auto dset = dataset_;
    auto p = std::make_shared<JobProgress>();
    p->total = estimateSoundingReads(*dset);
    beginJob(tr("Updating sounding…"), p);
    submitCompute<analysis::Sounding>(
        *pool_, this,
        [dset, t, mem, point, p] { return MainWindow::computeSounding(*dset, t, mem, point, p); },
        [this, view, point, tab, key, p](analysis::Sounding ns) {
            endJob(p);
            tab->inFlight = false;
            if (ns.levels.size() >= 2) {
                tab->cache[key] = ns;
                if (view) view->setSounding(ns);
            }
            if (tab->pending) {
                tab->pending = false;
                refreshSoundingTab(view, point, tab);
            }
            maybeAdvancePlayback();  // this tab's load settled → maybe advance
        });
}

void MainWindow::onCrossSectionRequested(const std::vector<core::LatLon>& path) {
    if (currentVar_.empty() || !dataset_ || currentTimes_.empty()) return;
    const std::string var = currentVar_;
    const core::TimePoint time = currentTimes_[static_cast<std::size_t>(timeIdx_)];
    const int member = currentMember_;
    auto ds = dataset_;
    auto prog = std::make_shared<JobProgress>();
    prog->total = estimateCrossSectionReads(*ds, var);
    beginJob(tr("Extracting cross-section…"), prog);
    submitCompute<analysis::CrossSection>(
        *pool_, this,
        [ds, var, time, member, path, prog] {
            return MainWindow::computeCrossSection(*ds, var, time, member, path, 200, prog);
        },
        [this, var, path, time, member, prog](analysis::CrossSection cs) {
            endJob(prog);
            if (cs.pressures.size() < 2) {
                statusBar()->showMessage(tr("Cross-section needs a multi-level variable"), 4000);
                return;
            }
            auto* view = new CrossSectionView;
            auto* frame = wrapCrossSection(view);  // panel + legend wired first
            view->setSection(cs);                  // emits rangeChanged -> fills the legend
            addAnalysisDock(frame, tr("Section: %1").arg(QString::fromStdString(var)));
            statusBar()->clearMessage();
            // Follow the time slider via a coalesced + cached re-extraction. Seed the
            // cache with this initial section so revisiting this time is instant.
            auto tab = std::make_shared<CrossSectionTab>();
            tab->epoch = datasetEpoch_;
            tab->cache[std::make_pair(static_cast<std::int64_t>(time.epochSeconds), member)] = cs;
            analyses_.push_back(
                {frame,
                 [this, v = QPointer<CrossSectionView>(view), var, path, tab]() {
                     refreshCrossSectionTab(v, var, path, tab);
                 },
                 [tab]() { return tab->inFlight; }});
        });
}

void MainWindow::onSoundingRequested(core::LatLon point) {
    if (!dataset_ || currentTimes_.empty()) return;
    const core::TimePoint time = currentTimes_[static_cast<std::size_t>(timeIdx_)];
    const int member = currentMember_;
    auto ds = dataset_;
    auto prog = std::make_shared<JobProgress>();
    prog->total = estimateSoundingReads(*ds);
    beginJob(tr("Extracting sounding…"), prog);
    submitCompute<analysis::Sounding>(
        *pool_, this,
        [ds, time, member, point, prog] {
            return MainWindow::computeSounding(*ds, time, member, point, prog);
        },
        [this, point, time, member, prog](analysis::Sounding s) {
            endJob(prog);
            if (s.levels.size() < 2) {
                statusBar()->showMessage(tr("Sounding needs multi-level temperature (t)"), 4000);
                return;
            }
            auto* view = new SkewTView;
            view->setSounding(s);
            addAnalysisDock(view, tr("Skew-T"));
            statusBar()->clearMessage();
            // Follow the time slider via a coalesced + cached re-extraction. Seed the
            // cache with this initial sounding so revisiting this time is instant.
            auto tab = std::make_shared<SoundingTab>();
            tab->epoch = datasetEpoch_;
            tab->cache[std::make_pair(static_cast<std::int64_t>(time.epochSeconds), member)] = s;
            analyses_.push_back({view,
                                 [this, v = QPointer<SkewTView>(view), point, tab]() {
                                     refreshSoundingTab(v, point, tab);
                                 },
                                 [tab]() { return tab->inFlight; }});
        });
}

void MainWindow::onTimeSeriesRequested(core::LatLon point) {
    if (currentVar_.empty() || !dataset_ || currentLevels_.empty()) return;
    const std::string var = currentVar_;
    const core::VerticalLevel level = currentLevels_[static_cast<std::size_t>(levelIdx_)];
    const int member = currentMember_;
    auto ds = dataset_;
    auto prog = std::make_shared<JobProgress>();
    if (const auto* entry = ds->catalog().find(var)) prog->total = static_cast<int>(entry->times.size());
    beginJob(tr("Extracting time series…"), prog);
    auto tick = [prog] { prog->done.fetch_add(1, std::memory_order_relaxed); };
    submitCompute<analysis::TimeSeries>(
        *pool_, this,
        [ds, var, level, member, tick, point] {
            return analysis::extractTimeSeries(
                MainWindow::readTimeStack(*ds, var, level, member, tick), point);
        },
        [this, var, prog](analysis::TimeSeries ts) {
            endJob(prog);
            if (ts.values.size() < 2) {
                statusBar()->showMessage(tr("Time series needs multiple time steps"), 4000);
                return;
            }
            auto* view = new TimeSeriesView;
            view->setSeries(ts, QString::fromStdString(var));
            view->setCurrentIndex(timeIdx_);
            addAnalysisDock(view, tr("Series: %1").arg(QString::fromStdString(var)));
            statusBar()->clearMessage();
            // The series spans all times; just move its marker with the slider.
            analyses_.push_back({view,
                                 [this, v = QPointer<TimeSeriesView>(view)]() {
                                     if (v) v->setCurrentIndex(timeIdx_);
                                 },
                                 []() { return false; }});  // marker move only; never "loading"
        });
}

void MainWindow::demoCrossSection() {
    // A short slanted transect centered on the demo point (stays inside typical
    // regional domains): ~8° of latitude by ~16° of longitude.
    const core::LatLon a{demoPoint_.lat + 4.0, demoPoint_.lon - 8.0};
    const core::LatLon b{demoPoint_.lat - 4.0, demoPoint_.lon + 8.0};
    onCrossSectionRequested({a, b});
}
void MainWindow::demoSounding() { onSoundingRequested(demoPoint_); }
void MainWindow::demoTimeSeries() { onTimeSeriesRequested(demoPoint_); }

void MainWindow::demoTiledLayout() {
    // Build a cross-section and a skew-T; addAnalysisDock() tiles them side by side
    // once both have been created (the extractions run asynchronously).
    tilePending_ = 2;
    tileFirst_ = nullptr;
    demoCrossSection();
    demoSounding();
}

QDockWidget* MainWindow::addAnalysisDock(QWidget* frame, const QString& title) {
    auto* dock = new QDockWidget(title, viewArea_);
    dock->setObjectName(QStringLiteral("analysisDock%1").arg(analysisSeq_++));  // for saveState()
    dock->setWidget(frame);
    dock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable |
                      QDockWidget::DockWidgetClosable);
    dock->setAttribute(Qt::WA_DeleteOnClose);  // closing deletes the dock + its view
    viewArea_->addDockWidget(Qt::LeftDockWidgetArea, dock);
    viewArea_->tabifyDockWidget(mapDock_, dock);  // tab with the existing views; drag to split
    dock->show();
    dock->raise();
    // Closed docks are pruned lazily: the QPointer in analyses_ nulls when the dock
    // is deleted, and refreshAnalyses() drops null entries.

    // --tile: tile the next two analysis docks side by side once both exist.
    if (tilePending_ > 0) {
        --tilePending_;
        if (!tileFirst_) {
            tileFirst_ = dock;  // wait for the second one
        } else {
            // Defer to the next tick so we're not rearranging docks reentrantly from
            // inside this dock's own creation. Hide the base 2D Plot/Map, leave `a` as
            // the sole in-layout dock (splitDockWidget only splits a non-tabbed
            // reference), then split `b` beside it.
            QDockWidget* a = tileFirst_;
            QDockWidget* b = dock;
            tileFirst_ = nullptr;
            QTimer::singleShot(0, this, [this, a, b]() {
                // Empty the area, then re-dock `a` (removing its tab-siblings leaves it
                // floating, so add it back explicitly) and split `b` beside it.
                for (QDockWidget* d : {plotDock_, mapDock_, a, b})
                    viewArea_->removeDockWidget(d);
                viewArea_->addDockWidget(Qt::LeftDockWidgetArea, a);
                viewArea_->splitDockWidget(a, b, Qt::Horizontal);
                a->show(); a->raise();
                b->show(); b->raise();
                // Even the split — otherwise the cross-section (which carries a control
                // panel) claims most of the width and squeezes the skew-T.
                const int half = viewArea_->width() / 2;
                viewArea_->resizeDocks({a, b}, {half, half}, Qt::Horizontal);
            });
        }
    }
    return dock;
}

void MainWindow::setContoursChecked(bool on) {
    if (plotContourCheck_) plotContourCheck_->setChecked(on);
}

void MainWindow::showMapTab() {
    if (mapDock_) {
        mapDock_->show();
        mapDock_->raise();
    }
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

void MainWindow::selectVariable(const QString& name) {
    if (!dataset_) return;
    const auto* entry = dataset_->catalog().find(name.toStdString());
    if (!entry) return;
    core::FieldKey key;
    key.varName = name.toStdString();
    if (!entry->levels.empty()) key.level = entry->levels.front();
    if (!entry->times.empty()) key.validTime = entry->times.front();
    onFieldChosen(key);  // populates the level/time combos and decodes
    if (datasetDock_) datasetDock_->selectField(name, key.level);
}

void MainWindow::selectLevelHpa(double hPa) {
    for (std::size_t i = 0; i < currentLevels_.size(); ++i) {
        const auto& lvl = currentLevels_[i];
        if (lvl.type == core::VerticalLevel::Type::PressureHPa &&
            std::abs(lvl.value - hPa) < 0.5) {
            if (levelCombo_) levelCombo_->setCurrentIndex(static_cast<int>(i));  // triggers onLevelChanged
            if (datasetDock_)
                datasetDock_->selectField(QString::fromStdString(currentVar_), lvl);
            return;
        }
    }
}

void MainWindow::setColormapByName(const QString& name) {
    for (QComboBox* c : {plotColormapCombo_, mapColormapCombo_}) {
        if (!c) continue;
        const int i = c->findText(name);
        if (i >= 0) c->setCurrentIndex(i);  // currentTextChanged repaints the view
    }
}

void MainWindow::setBasemapByName(const QString& name) {
    if (!mapBasemapCombo_) return;
    const int i = mapBasemapCombo_->findText(name);
    if (i >= 0) mapBasemapCombo_->setCurrentIndex(i);
}

void MainWindow::setDemoPoint(double lat, double lon) { demoPoint_ = {lat, lon}; }

void MainWindow::setTimeIndex(int index) {
    if (timeController_) timeController_->setCurrentIndex(index);
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
    // Restore the view-area split/tab arrangement of the base views (analysis docks
    // don't exist yet and are skipped by restoreState).
    if (s.contains("viewAreaState")) viewArea_->restoreState(s.value("viewAreaState").toByteArray());

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
    s.setValue("viewAreaState", viewArea_->saveState());
    s.setValue("colormap", mapColormapCombo_->currentText());
    s.setValue("basemap", mapBasemapCombo_->currentIndex());
    s.setValue("opacity", mapOpacitySlider_->value());
    s.setValue("graticule", mapGraticuleCheck_->isChecked());
    s.setValue("coastlines", mapCoastlineCheck_->isChecked());
    s.setValue("cacheBudgetMB", cacheBudgetMB_);
    s.setValue("animationFps", animationFps_);
}

void MainWindow::addRecentFile(const QString& path) {
    const QString abs = QFileInfo(path).absoluteFilePath();
    QSettings s;
    QStringList recent = s.value("recentFiles").toStringList();
    recent.removeAll(abs);   // move-to-front (most-recent-first, deduplicated)
    recent.prepend(abs);
    while (recent.size() > 10) recent.removeLast();
    s.setValue("recentFiles", recent);
    updateRecentMenu();
}

void MainWindow::updateRecentMenu() {
    if (!recentMenu_) return;
    recentMenu_->clear();
    const QStringList recent = QSettings().value("recentFiles").toStringList();
    if (recent.isEmpty()) {
        recentMenu_->addAction(tr("(no recent files)"))->setEnabled(false);
        return;
    }
    for (const QString& path : recent) {
        QAction* act = recentMenu_->addAction(QFileInfo(path).fileName());
        act->setToolTip(path);
        // Defer the open so this action isn't rebuilt/deleted inside its own
        // triggered() emission (openFile -> addRecentFile -> updateRecentMenu).
        connect(act, &QAction::triggered, this,
                [this, path] { QTimer::singleShot(0, this, [this, path] { openFile(path); }); });
    }
    recentMenu_->addSeparator();
    connect(recentMenu_->addAction(tr("Clear Recent")), &QAction::triggered, this, [this] {
        QTimer::singleShot(0, this, [this] {
            QSettings().remove("recentFiles");
            updateRecentMenu();
        });
    });
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
