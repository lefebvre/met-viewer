# Fuzz targets. Off by default and Clang-only: libFuzzer ships with Clang and has
# no GCC or MSVC equivalent, so this is not part of the normal build or of CI's
# two-compiler matrix. See the `fuzz` preset and the header comment in
# arl_fuzz.cpp for how to build and run it. The compiler check and the
# instrumentation of the libraries under test live with the option, in
# cmake/Sanitizers.cmake.

add_executable(arl_fuzz arl_fuzz.cpp)
target_include_directories(arl_fuzz PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(arl_fuzz PRIVATE met_options met_core met_readers)

# The harness alone links the libFuzzer main; everything it reaches is already
# coverage- and ASan-instrumented through met_options.
target_compile_options(arl_fuzz PRIVATE -fsanitize=fuzzer)
target_link_options(arl_fuzz PRIVATE -fsanitize=fuzzer)

# A short run over the seed corpus, wired into ctest so the harness itself cannot
# silently stop compiling or start crashing on the fixture. This is a smoke test,
# not a fuzzing campaign — real runs are long and interactive.
#
# libFuzzer writes every new interesting input into the *first* corpus directory
# it is given, so that one is a scratch directory in the build tree and the
# checked-in seeds come second, read-only. Pointing it at tests/fuzz/corpus first
# left new files in the source tree after every ctest run.
if(EXISTS ${CMAKE_SOURCE_DIR}/tests/fuzz/corpus)
    set(_met_fuzz_scratch ${CMAKE_CURRENT_BINARY_DIR}/corpus-scratch)
    file(MAKE_DIRECTORY ${_met_fuzz_scratch})
    add_test(NAME ArlFuzzSmoke
             COMMAND arl_fuzz -max_total_time=10 -print_final_stats=1
                     ${_met_fuzz_scratch} ${CMAKE_SOURCE_DIR}/tests/fuzz/corpus)
endif()
