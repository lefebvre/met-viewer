#include "viewer/app/mainwindow.h"

#include <cmath>

#include <QApplication>
#include <QComboBox>
#include <QDockWidget>
#include <QFileDialog>
#include <QFormLayout>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>
#include <QThreadPool>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include "viewer/app/colorbarwidget.h"
#include "viewer/app/datasetdock.h"
#include "viewer/app/jobs.h"
#include "viewer/app/plotview2d.h"
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
    // Central plot.
    plot_ = new PlotView2D(this);
    setCentralWidget(plot_);
    connect(plot_, &PlotView2D::probeMoved, this, &MainWindow::onProbeMoved);
    connect(plot_, &PlotView2D::probeLeft, this, &MainWindow::onProbeLeft);

    // Left: dataset browser.
    datasetDock_ = new DatasetDock(this);
    auto* leftDock = new QDockWidget(tr("Dataset"), this);
    leftDock->setWidget(datasetDock_);
    leftDock->setObjectName("datasetDock");
    addDockWidget(Qt::LeftDockWidgetArea, leftDock);
    connect(datasetDock_, &DatasetDock::fieldChosen, this, &MainWindow::onFieldChosen);

    // Right: inspector (colormap selector + colorbar).
    auto* inspector = new QWidget(this);
    auto* form = new QVBoxLayout(inspector);
    colormapCombo_ = new QComboBox(inspector);
    for (const auto& name : render::Colormap::builtinNames())
        colormapCombo_->addItem(QString::fromStdString(name));
    connect(colormapCombo_, &QComboBox::currentTextChanged, this, &MainWindow::onColormapChanged);
    colorbar_ = new ColorbarWidget(inspector);

    auto* cmapRow = new QWidget(inspector);
    auto* cmapForm = new QFormLayout(cmapRow);
    cmapForm->setContentsMargins(0, 0, 0, 0);
    cmapForm->addRow(tr("Colormap"), colormapCombo_);
    form->addWidget(cmapRow);
    form->addWidget(colorbar_, 1);

    auto* rightDock = new QDockWidget(tr("Inspector"), this);
    rightDock->setWidget(inspector);
    rightDock->setObjectName("inspectorDock");
    addDockWidget(Qt::RightDockWidgetArea, rightDock);

    // Menu.
    auto* fileMenu = menuBar()->addMenu(tr("&File"));
    QAction* openAct = fileMenu->addAction(tr("&Open…"));
    openAct->setShortcut(QKeySequence::Open);
    connect(openAct, &QAction::triggered, this, &MainWindow::onOpenTriggered);
    QAction* quitAct = fileMenu->addAction(tr("&Quit"));
    quitAct->setShortcut(QKeySequence::Quit);
    connect(quitAct, &QAction::triggered, this, &QWidget::close);

    // Status bar probe readout.
    probeLabel_ = new QLabel(tr("Ready"), this);
    statusBar()->addWidget(probeLabel_);
}

void MainWindow::scheduleGrabAndQuit(const QString& pngPath, int delayMs) {
    QTimer::singleShot(delayMs, this, [this, pngPath]() {
        // Grab the whole window so docks/colorbar/status are captured too.
        const QPixmap shot = grab();
        shot.save(pngPath);
        QApplication::quit();
    });
}

void MainWindow::onOpenTriggered() {
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open meteorological file"), {},
        tr("GRIB files (*.grib *.grib2 *.grb *.grb2);;All files (*)"));
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
    statusBar()->showMessage(tr("Opened %1 (%2)").arg(path).arg(
                                 QString::fromStdString(dataset_->formatName())),
                             4000);

    // Auto-display the first available field so the user sees something at once.
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

void MainWindow::onProbeMoved(double lat, double lon, double value, bool hasValue) {
    QString s = QStringLiteral("lat %1°  lon %2°").arg(lat, 0, 'f', 2).arg(lon, 0, 'f', 2);
    if (hasValue) {
        s += QStringLiteral("   %1 %2").arg(value, 0, 'f', 2).arg(currentUnits_);
        // Friendly alternate unit (e.g. K -> °C).
        const auto alt = met::core::preferredDisplayUnit(currentUnits_.toStdString());
        if (alt) {
            const auto converted =
                met::core::convert(value, currentUnits_.toStdString(), *alt);
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
