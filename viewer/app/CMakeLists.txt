find_package(Qt6 CONFIG REQUIRED COMPONENTS Widgets Network OpenGL OpenGLWidgets)

qt_standard_project_setup()

# App logic as a static library so it can be linked into unit tests. The
# executable is a thin main() on top; all widgets/orchestration live here.
add_library(met_app STATIC
    axisrange.h
    controlpanel.cpp
    extractions.cpp
    extractions.h
    exportname.cpp
    exportname.h
    figureexport.cpp
    figureexport.h
    controlpanel.h
    mainwindow.cpp
    mainwindow.h
    openpipeline.cpp
    openpipeline.h
    pointprofilecache.cpp
    pointprofilecache.h
    coalescingcache.h
    pointprofilemodel.cpp
    pointprofilemodel.h
    pointprofiledock.cpp
    pointprofiledock.h
    jobmonitor.cpp
    jobmonitor.h
    analysisdocks.cpp
    analysisdocks.h
    preferences.h
    plotview2d.cpp
    plotview2d.h
    mapview.cpp
    mapview.h
    glfieldrenderer.cpp
    glfieldrenderer.h
    tilelayer.cpp
    tilelayer.h
    coastlines.cpp
    coastlines.h
    icons.cpp
    icons.h
    colorbarwidget.cpp
    colorbarwidget.h
    hoverreadout.cpp
    hoverreadout.h
    crosssectionview.cpp
    crosssectionview.h
    skewtview.cpp
    skewtview.h
    timeseriesview.cpp
    timeseriesview.h
    datasetdock.cpp
    datasetdock.h
    timecontroller.cpp
    timecontroller.h
    theme.cpp
    theme.h
    jobs.cpp
    jobs.h
    fieldcache.cpp
    fieldcache.h
)
target_include_directories(met_app PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(met_app PUBLIC
    met_options
    met_core
    met_readers
    met_analysis
    met_render
    Qt6::Widgets
    Qt6::Network
    Qt6::OpenGL
    Qt6::OpenGLWidgets
)

add_executable(met_viewer
    main.cpp
    ${CMAKE_SOURCE_DIR}/resources/met-viewer.qrc
    # Embeds the app icon into the .exe (Explorer/taskbar); ignored elsewhere.
    $<$<PLATFORM_ID:Windows>:${CMAKE_SOURCE_DIR}/resources/win/met-viewer.rc>
)
target_link_libraries(met_viewer PRIVATE met_app)

# On macOS the executable is an app bundle, "Met Viewer.app", named as the
# Windows installer names the app. The icon is copied into Contents/Resources and
# named by CFBundleIconFile in resources/macos/Info.plist.in, which also carries
# the keys CMake's default template lacks. The bundle identifier is what macOS
# files the app's privacy permissions under, so it must not change between
# releases.
if(APPLE)
    set(_met_icns "${CMAKE_SOURCE_DIR}/resources/macos/met-viewer.icns")
    target_sources(met_viewer PRIVATE "${_met_icns}")
    set_source_files_properties("${_met_icns}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    set_target_properties(met_viewer PROPERTIES
        MACOSX_BUNDLE ON
        OUTPUT_NAME "Met Viewer"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/resources/macos/Info.plist.in"
        MACOSX_BUNDLE_BUNDLE_NAME "Met Viewer"
        MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.lefebvre.met-viewer"
        MACOSX_BUNDLE_ICON_FILE "met-viewer.icns"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_COPYRIGHT "met-viewer maintainers"
        # Where macdeployqt puts every dependency when the bundle is installed
        # (cmake/Packaging.cmake). The build tree keeps CMake's build RPATH into
        # vcpkg_installed, so the bundle there runs without deployment.
        INSTALL_RPATH "@executable_path/../Frameworks")
endif()

# Single source of truth for the version: project(VERSION) in the top-level
# CMakeLists. main.cpp used to carry its own hardcoded copy, which meant the
# version the app reports (--version, and the User-Agent the tile servers see)
# could silently drift from the one CPack stamps on the installer.
target_compile_definitions(met_viewer PRIVATE MET_VERSION="${PROJECT_VERSION}")

# AUTORCC compiles the .qrc listed in the sources above (app icon, coastline
# data). qt_standard_project_setup() enables AUTOMOC/AUTOUIC but NOT AUTORCC, so
# without this the resource is silently dropped and ":/..." lookups fail.
# On Windows, build a GUI subsystem app so launching from Explorer doesn't pop a
# console window. Qt supplies the WinMain->main entry point via Qt6::EntryPoint.
set_target_properties(met_viewer PROPERTIES
    AUTORCC ON
    WIN32_EXECUTABLE $<PLATFORM_ID:Windows>)

# Put Qt's plugins next to the executable on Windows. Without them the app
# cannot start from the build tree at all -- vcpkg's applocal copying handles
# only linked DLLs. See cmake/QtDeploy.cmake.
met_deploy_qt_runtime(met_viewer)
