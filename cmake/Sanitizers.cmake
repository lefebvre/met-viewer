option(MET_ENABLE_ASAN "Enable address/undefined sanitizers" OFF)
option(MET_ENABLE_TSAN "Enable the thread sanitizer" OFF)

# ASan and TSan are mutually exclusive by construction: both replace the same
# allocator and intercept the same runtime, and enabling them together produces a
# link that fails late and confusingly. Fail here, where the message can say why.
if(MET_ENABLE_ASAN AND MET_ENABLE_TSAN)
    message(FATAL_ERROR
        "MET_ENABLE_ASAN and MET_ENABLE_TSAN cannot both be ON: they are separate "
        "runtimes that cannot be linked into the same binary. Configure two build "
        "directories instead (see the asan-dist and tsan-dist presets).")
endif()

if(MET_ENABLE_ASAN)
    target_compile_options(met_options INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(met_options INTERFACE -fsanitize=address,undefined)
endif()

# TSan covers what ASan structurally cannot: the warp's row-chunk fan-out over a
# parked worker pool, the QThreadPool jobs with queued delivery, and the netcdf-c
# process-wide mutex are all races-or-nothing, and a data race there produces
# wrong numbers rather than a crash ASan would catch.
if(MET_ENABLE_TSAN)
    target_compile_options(met_options INTERFACE -fsanitize=thread -fno-omit-frame-pointer -g)
    target_link_options(met_options INTERFACE -fsanitize=thread)
endif()

# Clang-only (libFuzzer), so it is opt-in rather than part of the default build or
# of CI's GCC/MSVC matrix. See tests/fuzz/ and the `fuzz` preset.
option(MET_ENABLE_FUZZING "Build the libFuzzer harnesses (requires Clang)" OFF)
if(MET_ENABLE_FUZZING)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR
            "MET_ENABLE_FUZZING requires Clang (libFuzzer); current compiler is "
            "${CMAKE_CXX_COMPILER_ID}. Configure with -DCMAKE_CXX_COMPILER=clang++.")
    endif()
    if(MET_ENABLE_TSAN)
        message(FATAL_ERROR "MET_ENABLE_FUZZING runs under ASan and cannot be combined "
                            "with MET_ENABLE_TSAN.")
    endif()
    # The code under test has to be instrumented, not just the harness. Coverage
    # (fuzzer-no-link) is what lets libFuzzer steer mutations into the decoder's
    # branches, and ASan in the decoder is what reports a misaligned read inside
    # it. With both applied only to the harness file, the fuzzer mutated blind and
    # a read past a buffer in arldataset.cpp went unreported. The libFuzzer main
    # itself is linked into the harness alone (tests/fuzz/CMakeLists.txt).
    target_compile_options(met_options INTERFACE
        -fsanitize=fuzzer-no-link,address -fno-omit-frame-pointer -g)
    target_link_options(met_options INTERFACE -fsanitize=address)
endif()
