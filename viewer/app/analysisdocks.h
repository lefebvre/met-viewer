#pragma once

#include <QObject>
#include <QString>

class QDockWidget;
class QMainWindow;
class QWidget;

namespace met::app {

// Creates and places the closable analysis panels (cross-section, skew-T, time
// series) inside the nested view-area QMainWindow.
//
// This is dock plumbing, not window logic: naming docks for saveState(), tabbing
// them onto the base views, and the deferred two-dock split that `--tile` asks for.
// It sat in MainWindow only because that is where the dock pointers were, and it
// grew a pair of bare members (`tilePending_`, `tileFirst_`) that meant nothing to
// anything else in the class. Here the pending-tile state is scoped to the object
// that uses it.
class AnalysisDockFactory : public QObject {
    Q_OBJECT
public:
    // `viewArea` is the nested QMainWindow the docks live in; `tabWith` is the base
    // dock new panels are tabbed onto; `baseDocks` are the base views to hide when
    // a tiled layout takes over the area. All are owned by the caller.
    AnalysisDockFactory(QMainWindow* viewArea, QDockWidget* tabWith, QDockWidget* plotDock,
                        QObject* parent = nullptr);

    // Add `frame` as a closable, dockable, floatable panel. Deleted on close.
    QDockWidget* add(QWidget* frame, const QString& title);

    // Ask for the next `count` docks to be arranged side by side once they all
    // exist. The panels are created asynchronously (each waits on an extraction),
    // so the arrangement cannot happen until the last one arrives.
    void tileNext(int count);

private:
    void applyTiling(QDockWidget* dock);

    QMainWindow* viewArea_ = nullptr;
    QDockWidget* tabWith_ = nullptr;
    QDockWidget* plotDock_ = nullptr;
    int seq_ = 0;          // unique object-name counter, for saveState()
    int tilePending_ = 0;  // docks still to come before the split can happen
    QDockWidget* tileFirst_ = nullptr;
};

}  // namespace met::app
