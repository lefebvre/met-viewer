#include <QApplication>

#include "viewer/app/mainwindow.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    met::app::MainWindow window;
    window.show();

    return QApplication::exec();
}
