#include "viewer/app/mainwindow.h"

namespace met::app {

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(tr("met-viewer"));
    resize(1280, 800);
}

}  // namespace met::app
