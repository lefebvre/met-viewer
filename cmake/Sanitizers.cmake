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
