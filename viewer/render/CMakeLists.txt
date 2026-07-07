find_package(Qt6 CONFIG REQUIRED COMPONENTS Gui)

add_library(met_render STATIC
    render.cpp
)

target_include_directories(met_render PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(met_render PUBLIC met_options Qt6::Gui)
