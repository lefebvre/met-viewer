add_library(met_options INTERFACE)
target_compile_features(met_options INTERFACE cxx_std_20)

target_compile_options(met_options INTERFACE
    -Wall
    -Wextra
    -Wpedantic
    -Wconversion
    -Wsign-conversion
    -Wshadow
)
