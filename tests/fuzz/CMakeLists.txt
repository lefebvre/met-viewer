# Fuzz targets. Off by default and Clang-only: libFuzzer ships with Clang and has
# no GCC or MSVC equivalent, so this is not part of the normal build or of CI's
# two-compiler matrix. See the `fuzz` preset and the header comment in
# arl_fuzz.cpp for how to build and run it.
if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR
        "MET_ENABLE_FUZZING requires Clang (libFuzzer); current compiler is "
        "${CMAKE_CXX_COMPILER_ID}. Configure with -DCMAKE_CXX_COMPILER=clang++.")
endif()

add_executable(arl_fuzz arl_fuzz.cpp)
target_include_directories(arl_fuzz PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(arl_fuzz PRIVATE met_options met_core met_readers)

# ASan alongside the fuzzer: libFuzzer finds the input, ASan is what turns a
# misaligned read into a report instead of a plausible-looking number.
target_compile_options(arl_fuzz PRIVATE -fsanitize=fuzzer,address -fno-omit-frame-pointer -g)
target_link_options(arl_fuzz PRIVATE -fsanitize=fuzzer,address)

# A short run over the seed corpus, wired into ctest so the harness itself cannot
# silently stop compiling or start crashing on the fixture. This is a smoke test,
# not a fuzzing campaign — real runs are long and interactive.
if(EXISTS ${CMAKE_SOURCE_DIR}/tests/fuzz/corpus)
    add_test(NAME ArlFuzzSmoke
             COMMAND arl_fuzz -max_total_time=10 -print_final_stats=1
                     ${CMAKE_SOURCE_DIR}/tests/fuzz/corpus)
endif()
