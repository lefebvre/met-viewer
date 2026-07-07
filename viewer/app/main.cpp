#include <QApplication>

#include "viewer/app/mainwindow.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("met-viewer");

    met::app::MainWindow window;
    window.show();

    // Args: [file] [--grab out.png]
    const QStringList args = QApplication::arguments();
    QString grabPath;
    for (int i = 1; i < args.size(); ++i) {
        if (args.at(i) == "--grab" && i + 1 < args.size()) {
            grabPath = args.at(++i);
        } else if (!args.at(i).startsWith("--")) {
            window.openFile(args.at(i));
        }
    }
    if (!grabPath.isEmpty()) window.scheduleGrabAndQuit(grabPath);

    return QApplication::exec();
}
