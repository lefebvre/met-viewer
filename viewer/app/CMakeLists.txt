find_package(Qt6 CONFIG REQUIRED COMPONENTS Widgets Network OpenGLWidgets)

qt_standard_project_setup()

add_executable(met_viewer
    main.cpp
    mainwindow.cpp
    mainwindow.h
)

target_include_directories(met_viewer PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(met_viewer PRIVATE
    met_options
    met_core
    met_readers
    met_analysis
    met_render
    Qt6::Widgets
    Qt6::Network
    Qt6::OpenGLWidgets
)
