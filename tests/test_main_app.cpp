#include <gtest/gtest.h>

#include <QApplication>
#include <QSettings>
#include <QTemporaryDir>

// App-layer tests construct QWidgets, which require a QApplication and a platform
// plugin. Force the headless "minimal" platform before QApplication so both test
// discovery (--gtest_list_tests) and the test run work without a display.
int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", QByteArray("minimal"));
    // The open-pipeline tests decode real GRIB fixtures, so ecCodes needs its
    // definitions regardless of the path compiled into the library. Set before any
    // ecCodes context exists, i.e. before the tests run. Mirrors test_main.cpp.
#ifdef MET_ECCODES_DEFINITION_PATH
    qputenv("ECCODES_DEFINITION_PATH", QByteArray(MET_ECCODES_DEFINITION_PATH));
#endif
#ifdef MET_ECCODES_SAMPLES_PATH
    qputenv("ECCODES_SAMPLES_PATH", QByteArray(MET_ECCODES_SAMPLES_PATH));
#endif
    // Give this process its own settings store. App code persists state in
    // QSettings (unit choices, hover flags, window geometry), and ctest runs each
    // test case as a separate process, several at a time -- against one store per
    // user, a case reads what a concurrent one wrote and fails on a value it never
    // set. A store per process also means a case that dies partway cannot leave a
    // choice behind for the next run.
    //
    // The path has to be an explicit format: setPath does not redirect the native
    // Windows store, which is the registry.
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QTemporaryDir settingsDir;
    if (!settingsDir.isValid()) {
        qCritical("cannot create a private settings directory: %s",
                  qPrintable(settingsDir.errorString()));
        return 1;
    }
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());

    // Scope QSettings to a test-only organization so app code that persists state
    // never reads or clobbers the developer's own met-viewer settings.
    QApplication::setApplicationName("met-viewer-tests");
    QApplication::setOrganizationName("met-viewer-tests");
    QApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
