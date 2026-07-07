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

# Promote warnings to errors on demand (enabled by the asan/CI preset) so the
# strong warning set above actually gates the build.
option(MET_WERROR "Treat compiler warnings as errors" OFF)
if(MET_WERROR)
    target_compile_options(met_options INTERFACE -Werror)
endif()
