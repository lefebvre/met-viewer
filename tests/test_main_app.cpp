#include <gtest/gtest.h>

#include <QApplication>

// App-layer tests construct QWidgets, which require a QApplication and a platform
// plugin. Force the headless "minimal" platform before QApplication so both test
// discovery (--gtest_list_tests) and the test run work without a display.
int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", QByteArray("minimal"));
    // Scope QSettings to a test-only organization so app code that persists state
    // (HoverOptions, window geometry) never reads or clobbers the developer's own
    // met-viewer settings.
    QApplication::setApplicationName("met-viewer-tests");
    QApplication::setOrganizationName("met-viewer-tests");
    QApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
