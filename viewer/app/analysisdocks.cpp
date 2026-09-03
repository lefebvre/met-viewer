#include "viewer/app/analysisdocks.h"

#include <QDockWidget>
#include <QMainWindow>
#include <QTimer>
#include <QWidget>

namespace met::app {

AnalysisDockFactory::AnalysisDockFactory(QMainWindow* viewArea, QDockWidget* tabWith,
                                         QDockWidget* plotDock, QObject* parent)
    : QObject(parent), viewArea_(viewArea), tabWith_(tabWith), plotDock_(plotDock) {}

void AnalysisDockFactory::tileNext(int count) {
    tilePending_ = count;
    tileFirst_ = nullptr;
}

QDockWidget* AnalysisDockFactory::add(QWidget* frame, const QString& title) {
    auto* dock = new QDockWidget(title, viewArea_);
    dock->setObjectName(QStringLiteral("analysisDock%1").arg(seq_++));  // for saveState()
    dock->setWidget(frame);
    dock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable |
                      QDockWidget::DockWidgetClosable);
    dock->setAttribute(Qt::WA_DeleteOnClose);  // closing deletes the dock + its view
    viewArea_->addDockWidget(Qt::LeftDockWidgetArea, dock);
    viewArea_->tabifyDockWidget(tabWith_, dock);  // tab with the existing views; drag to split
    dock->show();
    dock->raise();
    // Closed docks are pruned lazily by the owner: its QPointer nulls when the dock
    // is deleted, and its refresh pass drops null entries.

    applyTiling(dock);
    return dock;
}

void AnalysisDockFactory::applyTiling(QDockWidget* dock) {
    if (tilePending_ <= 0) return;
    --tilePending_;
    if (!tileFirst_) {
        tileFirst_ = dock;  // wait for the second one
        return;
    }

    // Defer to the next tick so we're not rearranging docks reentrantly from inside
    // this dock's own creation. Hide the base 2D Plot/Map, leave `a` as the sole
    // in-layout dock (splitDockWidget only splits a non-tabbed reference), then
    // split `b` beside it.
    QDockWidget* a = tileFirst_;
    QDockWidget* b = dock;
    tileFirst_ = nullptr;
    QTimer::singleShot(0, this, [this, a, b]() {
        // Empty the area, then re-dock `a` (removing its tab-siblings leaves it
        // floating, so add it back explicitly) and split `b` beside it.
        for (QDockWidget* d : {plotDock_, tabWith_, a, b}) viewArea_->removeDockWidget(d);
        viewArea_->addDockWidget(Qt::LeftDockWidgetArea, a);
        viewArea_->splitDockWidget(a, b, Qt::Horizontal);
        a->show();
        a->raise();
        b->show();
        b->raise();
        // Even the split — otherwise whichever view asks for more width claims most
        // of it and squeezes the other.
        const int half = viewArea_->width() / 2;
        viewArea_->resizeDocks({a, b}, {half, half}, Qt::Horizontal);
    });
}

}  // namespace met::app
