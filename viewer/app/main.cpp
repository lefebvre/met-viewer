#include <QApplication>
#include <QSurfaceFormat>

#include "viewer/app/mainwindow.h"

int main(int argc, char** argv) {
    // Request a desktop OpenGL 3.3 compatibility context: floating-point (R32F)
    // textures for the field, and QPainter still works for tiles/overlays.
    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setRenderableType(QSurfaceFormat::OpenGL);
    fmt.setSamples(0);   // no MSAA: multisample-FBO resolve corrupts native GL
    fmt.setDepthBufferSize(0);
    fmt.setStencilBufferSize(0);
    QSurfaceFormat::setDefaultFormat(fmt);

    QApplication app(argc, argv);
    QApplication::setApplicationName("met-viewer");
    QApplication::setOrganizationName("met-viewer");
    QApplication::setOrganizationDomain("met-viewer.local");

    met::app::MainWindow window;
    window.show();

    // Args: [file] [--contours] [--map] [--grab out.png]
    const QStringList args = QApplication::arguments();
    QString grabPath;
    bool contours = false;
    bool map = false;
    int windMode = 0;
    int derivedMode = 0;
    QString demo;
    for (int i = 1; i < args.size(); ++i) {
        if (args.at(i) == "--grab" && i + 1 < args.size()) {
            grabPath = args.at(++i);
        } else if (args.at(i) == "--contours") {
            contours = true;
        } else if (args.at(i) == "--map") {
            map = true;
        } else if (args.at(i) == "--wind" && i + 1 < args.size()) {
            windMode = args.at(++i).toInt();
        } else if (args.at(i) == "--derived" && i + 1 < args.size()) {
            derivedMode = args.at(++i).toInt();
        } else if (args.at(i) == "--demo" && i + 1 < args.size()) {
            demo = args.at(++i);
        } else if (args.at(i) == "--play") {
            demo = "play";
        } else if (args.at(i) == "--cpu") {
            window.setGpuChecked(false);
        } else if (!args.at(i).startsWith("--")) {
            window.openFile(args.at(i));
        }
    }
    if (contours) window.setContoursChecked(true);
    if (map) window.showMapTab();
    if (windMode > 0) window.setWindComboIndex(windMode);
    if (derivedMode > 0) window.setDerivedComboIndex(derivedMode);
    if (demo == "section") window.demoCrossSection();
    else if (demo == "sounding") window.demoSounding();
    else if (demo == "series") window.demoTimeSeries();
    else if (demo == "play") window.startPlayback();
    // Give the map extra time to fetch tiles before a headless grab.
    if (!grabPath.isEmpty()) window.scheduleGrabAndQuit(grabPath, map ? 3500 : 1200);

    return QApplication::exec();
}
