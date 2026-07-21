#include <cstdio>
#include <cstdlib>

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QString>
#include <QStringList>
#include <QSurfaceFormat>
#include <QtGlobal>

#include "viewer/app/mainwindow.h"
#include "viewer/app/theme.h"
#include "viewer/core/crs.h"

namespace {

QtMessageHandler g_prevHandler = nullptr;

// Suppress a few known-benign warnings this (ICU-less) Qt build emits and pass
// everything else through unchanged:
//   - "... posix collation implementation" — QFileDialog's QCollator falls back
//     to POSIX collation when Qt is built without ICU; numeric/case-insensitive
//     sorting isn't supported there, but the dialog still works.
//   - "QSslSocket): device not open" — a benign read on an HTTPS tile socket that
//     is already closed during reply teardown/abort or app quit.
// The filters match exact substrings, so real SSL/handshake errors still surface.
void filterQtWarnings(QtMsgType type, const QMessageLogContext& ctx, const QString& msg) {
    if (msg.contains(QLatin1String("posix collation implementation")) ||
        msg.contains(QLatin1String("QSslSocket): device not open"))) {
        return;
    }
    if (g_prevHandler) {
        g_prevHandler(type, ctx, msg);
        return;
    }
    std::fprintf(stderr, "%s\n", msg.toLocal8Bit().constData());
    std::fflush(stderr);
    if (type == QtFatalMsg) std::abort();
}

// Point PROJ at bundled data in installed/portable builds. The compile-time
// MET_PROJ_DATA fallback covers the dev build tree, but that absolute path does
// not exist on an end user's machine, so probe a few executable-relative layouts
// (installed prefix, AppImage AppDir, Windows install root) for proj.db and, if
// found, register it. If nothing matches we leave the compile-time fallback in
// place. Must run after QApplication so applicationDirPath() is valid.
void locateBundledProjData() {
    const QString exeDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        exeDir + QStringLiteral("/../share/proj"),  // GNUInstallDirs / AppImage usr/bin
        exeDir + QStringLiteral("/share/proj"),      // flat install with a share/ subdir
        exeDir + QStringLiteral("/proj"),            // proj data staged next to the exe
    };
    for (const QString& c : candidates) {
        if (QFileInfo::exists(c + QStringLiteral("/proj.db"))) {
            met::core::setProjDataPath(QDir(c).absolutePath().toStdString());
            return;
        }
    }
}

// Build the application/window icon from the embedded PNG set. Adding every
// size lets Qt pick the sharpest source per surface (window frame, taskbar,
// Alt-Tab) and device-pixel-ratio instead of scaling a single bitmap. The light
// variant (line art on transparency) is used in the app's dark theme for
// contrast on dark title bars.
QIcon appWindowIcon(bool useLight) {
    const QString suffix = useLight ? QStringLiteral("-light") : QString();
    QIcon icon;
    for (int px : {16, 24, 32, 48, 64, 128, 256}) {
        icon.addFile(QStringLiteral(":/icons/app/met-viewer_%1%2.png").arg(px).arg(suffix));
    }
    return icon;
}

}  // namespace

int main(int argc, char** argv) {
    g_prevHandler = qInstallMessageHandler(filterQtWarnings);

    // Request a desktop OpenGL 3.3 core context. The GPU field path uploads a
    // colormapped RGBA8 texture (float textures were ruled out); QPainter still
    // works for tiles/overlays.
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
    QApplication::setApplicationVersion("0.1.0");
    QApplication::setOrganizationName("met-viewer");
    QApplication::setOrganizationDomain("met-viewer.local");
    QApplication::setWindowIcon(appWindowIcon(false));
    // Ties the window to met-viewer.desktop so Wayland/GNOME (and other DEs that
    // match by app-id rather than _NET_WM_ICON) show the app icon in the dock and
    // task switcher. Requires met-viewer.desktop + the themed icon to be
    // installed (the package/AppImage does this).
    QApplication::setDesktopFileName(QStringLiteral("met-viewer"));

    // Resolve PROJ data relative to the executable for installed/bundled builds.
    locateBundledProjData();

    met::app::MainWindow window;
    // Swap the window-icon variant with the app theme (light art on dark title
    // bars). Set it now for the resolved startup theme, then track changes.
    QApplication::setWindowIcon(appWindowIcon(window.theme()->isDark()));
    QObject::connect(window.theme(), &met::app::ThemeManager::effectiveSchemeChanged, &app,
                     [](bool dark) { QApplication::setWindowIcon(appWindowIcon(dark)); });
    window.show();

    QCommandLineParser parser;
    parser.setApplicationDescription(
        "met-viewer — view and analyze gridded meteorological data (GRIB, NetCDF, ARL).");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(
        "file", "Data file(s) to open, loaded together as one time-stepped dataset.", "[file...]");
    parser.addOptions({
        {"map", "Start on the GIS Map tab."},
        {"contours", "Enable the 2D-plot contour overlay."},
        {"wind", "Wind overlay mode: 1 barbs, 2 streamlines.", "mode"},
        {"derived", "Select a derived quantity by index.", "index"},
        {"demo", "Open an analysis view on the demo point: section|sounding|series.", "view"},
        {"tile", "Tile a cross-section beside a skew-T to show a split layout."},
        {"size", "Window size WxH in pixels (e.g. 1600x840).", "WxH"},
        {"time", "Jump to a time-step index (e.g. for rendering GIF frames).", "index"},
        {"play", "Start time-animation playback."},
        {"gpu", "Force the GPU map-warp path."},
        {"cpu", "Force the CPU map-warp path."},
        {"grab", "Render the window to a PNG and quit (headless screenshot).", "out.png"},
        {"var", "Select the displayed variable by short name (e.g. t, u).", "name"},
        {"level", "Select the pressure level in hPa (e.g. 500).", "hPa"},
        {"colormap", "Set the colormap by name (e.g. turbo, \"RdBu (diverging)\").", "name"},
        {"basemap", "Set the map basemap by name (e.g. \"Carto Dark\").", "name"},
        {"demo-at", "Sample point \"LAT,LON\" the --demo triggers use.", "lat,lon"},
    });
    parser.process(app);

    if (parser.isSet("size")) {
        const QStringList wh = parser.value("size").split('x', Qt::SkipEmptyParts);
        if (wh.size() == 2) window.resize(wh.at(0).toInt(), wh.at(1).toInt());
    }

    // Positional args open together as one merged, time-stepped dataset. Blocking
    // so the follow-on flags below (--var/--time/--grab) act on a ready dataset.
    window.openFilesBlocking(parser.positionalArguments());

    // Field selection first, so map/contours/wind/demo act on the intended field.
    if (parser.isSet("var")) window.selectVariable(parser.value("var"));
    if (parser.isSet("level")) window.selectLevelHpa(parser.value("level").toDouble());
    if (parser.isSet("colormap")) window.setColormapByName(parser.value("colormap"));
    if (parser.isSet("contours")) window.setContoursChecked(true);
    if (parser.isSet("wind")) window.setWindComboIndex(parser.value("wind").toInt());
    if (parser.isSet("derived")) window.setDerivedComboIndex(parser.value("derived").toInt());
    if (parser.isSet("cpu")) window.setGpuChecked(false);
    if (parser.isSet("gpu")) window.setGpuChecked(true);
    if (parser.isSet("basemap")) window.setBasemapByName(parser.value("basemap"));

    const bool map = parser.isSet("map");
    if (map) window.showMapTab();
    if (parser.isSet("time")) window.setTimeIndex(parser.value("time").toInt());

    if (parser.isSet("demo-at")) {
        const QStringList xy = parser.value("demo-at").split(',');
        if (xy.size() == 2) window.setDemoPoint(xy.at(0).toDouble(), xy.at(1).toDouble());
    }

    if (parser.isSet("tile")) window.demoTiledLayout();

    QString demo = parser.value("demo");
    if (parser.isSet("play")) demo = "play";
    if (demo == "section") window.demoCrossSection();
    else if (demo == "sounding") window.demoSounding();
    else if (demo == "series") window.demoTimeSeries();
    else if (demo == "play") window.startPlayback();

    // Give the map extra time to fetch tiles before a headless grab.
    if (parser.isSet("grab")) window.scheduleGrabAndQuit(parser.value("grab"), map ? 3500 : 1200);

    return QApplication::exec();
}
