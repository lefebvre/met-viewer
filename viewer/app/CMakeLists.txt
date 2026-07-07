find_package(Qt6 CONFIG REQUIRED COMPONENTS Widgets Network OpenGL OpenGLWidgets)

qt_standard_project_setup()

# App logic as a static library so it can be linked into unit tests. The
# executable is a thin main() on top; all widgets/orchestration live here.
add_library(met_app STATIC
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
)
target_link_libraries(met_viewer PRIVATE met_app)
