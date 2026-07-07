find_package(Qt6 CONFIG REQUIRED COMPONENTS Gui)

find_package(Threads REQUIRED)

add_library(met_render STATIC
    colormap.cpp
    fieldimage.cpp
    contour.cpp
    warp.cpp
)

target_include_directories(met_render PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(met_render PUBLIC met_options met_core Qt6::Gui PRIVATE Threads::Threads)
