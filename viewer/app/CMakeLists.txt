find_package(Qt6 CONFIG REQUIRED COMPONENTS Widgets Network OpenGL OpenGLWidgets)

qt_standard_project_setup()

# App logic as a static library so it can be linked into unit tests. The
# executable is a thin main() on top; all widgets/orchestration live here.
add_library(met_app STATIC
    controlpanel.cpp
    controlpanel.h
    mainwindow.cpp
    mainwindow.h
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
    colorbarwidget.cpp
    colorbarwidget.h
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

# AUTORCC compiles the .qrc listed in the sources above (app icon, coastline
# data). qt_standard_project_setup() enables AUTOMOC/AUTOUIC but NOT AUTORCC, so
# without this the resource is silently dropped and ":/..." lookups fail.
# On Windows, build a GUI subsystem app so launching from Explorer doesn't pop a
# console window. Qt supplies the WinMain->main entry point via Qt6::EntryPoint.
set_target_properties(met_viewer PROPERTIES
    AUTORCC ON
    WIN32_EXECUTABLE $<PLATFORM_ID:Windows>)
