#include <QApplication>

#include "viewer/app/mainwindow.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("met-viewer");

    met::app::MainWindow window;
    window.show();

    // Args: [file] [--contours] [--grab out.png]
    const QStringList args = QApplication::arguments();
    QString grabPath;
    bool contours = false;
    for (int i = 1; i < args.size(); ++i) {
        if (args.at(i) == "--grab" && i + 1 < args.size()) {
            grabPath = args.at(++i);
        } else if (args.at(i) == "--contours") {
            contours = true;
        } else if (!args.at(i).startsWith("--")) {
            window.openFile(args.at(i));
        }
    }
    if (contours) window.setContoursChecked(true);
    if (!grabPath.isEmpty()) window.scheduleGrabAndQuit(grabPath);

    return QApplication::exec();
}
