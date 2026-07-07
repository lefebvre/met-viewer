option(MET_ENABLE_ASAN "Enable address/undefined sanitizers" OFF)

if(MET_ENABLE_ASAN)
    target_compile_options(met_options INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(met_options INTERFACE -fsanitize=address,undefined)
endif()
