add_library(met_options INTERFACE)
target_compile_features(met_options INTERFACE cxx_std_20)

# GCC/Clang take -W flags; MSVC (cl) rejects them (error D8021) and uses /W
# flags, so select the right dialect per compiler.
if(MSVC)
    target_compile_options(met_options INTERFACE /W4 /permissive-)
else()
    target_compile_options(met_options INTERFACE
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wsign-conversion
        -Wshadow
    )
endif()

# Promote warnings to errors on demand (enabled by the asan/CI preset) so the
# strong warning set above actually gates the build.
option(MET_WERROR "Treat compiler warnings as errors" OFF)
if(MET_WERROR)
    if(MSVC)
        target_compile_options(met_options INTERFACE /WX)
    else()
        target_compile_options(met_options INTERFACE -Werror)
    endif()
endif()
