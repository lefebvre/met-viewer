# Packaging: install rules + CPack configuration for distributable installers
# (AppImage / TGZ on Linux, NSIS on Windows). Included from the top-level
# CMakeLists.txt after every target is defined so install(TARGETS ...) resolves.
#
# Deployment of the Qt runtime is per-platform: Windows runs windeployqt as part
# of the install (below), so `cmake --install` and cpack both yield a
# self-contained tree; Linux leaves it to linuxdeploy, which bundles Qt when it
# assembles the AppImage in CI.

include(GNUInstallDirs)

# --- Application executable ---------------------------------------------------
install(TARGETS met_viewer
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# --- Qt runtime bundling ------------------------------------------------------
# On Windows, windeployqt copies the Qt DLLs and plugins the executable needs next
# to it in the install tree, so both the NSIS package and the portable build are
# self-contained. Linux skips this: linuxdeploy does the equivalent when it builds
# the AppImage.
#
# It is invoked directly rather than through
# qt_generate_deploy_app_script(). Qt's wrapper ultimately just runs
# `windeployqt <exe>` (Qt6CoreDeploySupport.cmake), but it locates the tool with
# find_program hinted at ${QT6_INSTALL_PREFIX}/${QT6_INSTALL_BINS} — and vcpkg
# sets QT6_INSTALL_BINS to "bin", which holds the Qt DLLs, while the tools live in
# tools/Qt6/bin. The lookup therefore fails, and it fails during find_package, so
# it cannot be corrected afterwards. Qt then aborts the install with "No Qt deploy
# tool available for this target platform".
#
# Calling the tool ourselves does the same work, fails at configure time instead
# of install time when it is missing, and does not depend on Qt internals or on
# the order in which find_package(Qt6) happens to run.
if(WIN32)
    find_program(MET_WINDEPLOYQT
        NAMES windeployqt windeployqt6
        HINTS "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/tools/Qt6/bin"
              "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
        DOC "windeployqt, used to bundle the Qt runtime next to the installed exe")
    if(NOT MET_WINDEPLOYQT)
        message(FATAL_ERROR
            "windeployqt not found. Installers and portable builds would ship "
            "without the Qt DLLs and fail to start on any machine without Qt.")
    endif()

    # Runs as part of `cmake --install` (and therefore of cpack), against the exe
    # already placed in the install tree, so both the NSIS package and the
    # portable build get the same self-contained payload.
    install(CODE "
        set(_met_exe \"\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/met_viewer.exe\")
        message(STATUS \"Deploying Qt runtime with ${MET_WINDEPLOYQT}\")
        execute_process(
            COMMAND \"${MET_WINDEPLOYQT}\" --no-translations \"\${_met_exe}\"
            RESULT_VARIABLE _met_deploy_result)
        if(NOT _met_deploy_result EQUAL 0)
            message(FATAL_ERROR \"windeployqt failed (\${_met_deploy_result})\")
        endif()")
endif()

# --- PROJ runtime data (proj.db etc.) ----------------------------------------
# Installed under <prefix>/share/proj so the executable-relative probe in
# locateBundledProjData() (main.cpp) finds it as <bindir>/../share/proj. Sourced
# from the vcpkg tree that built the app.
if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
    set(_met_proj_src "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/proj")
    if(EXISTS "${_met_proj_src}/proj.db")
        install(DIRECTORY "${_met_proj_src}/"
                DESTINATION "${CMAKE_INSTALL_DATADIR}/proj")
    else()
        message(WARNING
            "PROJ data not found at ${_met_proj_src}; installers will lack proj.db "
            "and CRS transforms may fail at runtime.")
    endif()
endif()

# --- Linux desktop integration (menu entry + themed icons) -------------------
if(UNIX AND NOT APPLE)
    install(FILES "${CMAKE_SOURCE_DIR}/resources/linux/met-viewer.desktop"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")
    # Install every app-icon size that exists into its hicolor directory, so the
    # set can be pruned/extended without editing this list.
    file(GLOB _met_app_icons
         "${CMAKE_SOURCE_DIR}/resources/icons/png/app/met-viewer_*.png")
    foreach(_icon IN LISTS _met_app_icons)
        if(_icon MATCHES "met-viewer_([0-9]+)\\.png$")
            set(_sz "${CMAKE_MATCH_1}")
            install(FILES "${_icon}"
                DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/${_sz}x${_sz}/apps"
                RENAME "met-viewer.png")
        endif()
    endforeach()
endif()

# --- CPack --------------------------------------------------------------------
set(CPACK_PACKAGE_NAME "met-viewer")
set(CPACK_PACKAGE_VENDOR "met-viewer")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "View and analyze gridded meteorological data (GRIB, NetCDF, ARL)")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "met-viewer")
set(CPACK_PACKAGE_CONTACT "met-viewer maintainers")
set(CPACK_STRIP_FILES ON)
set(CPACK_VERBATIM_VARIABLES ON)

if(WIN32)
    set(CPACK_GENERATOR "NSIS")
    set(CPACK_NSIS_PACKAGE_NAME "Met Viewer")
    set(CPACK_NSIS_DISPLAY_NAME "Met Viewer ${PROJECT_VERSION}")
    set(CPACK_NSIS_MUI_ICON "${CMAKE_SOURCE_DIR}/resources/icons/met-viewer.ico")
    set(CPACK_NSIS_MUI_UNIICON "${CMAKE_SOURCE_DIR}/resources/icons/met-viewer.ico")
    set(CPACK_NSIS_INSTALLED_ICON_NAME "${CMAKE_INSTALL_BINDIR}\\\\met_viewer.exe")
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
    set(CPACK_NSIS_MODIFY_PATH OFF)
    # Start-menu (and optional desktop) shortcut to the installed executable.
    set(CPACK_PACKAGE_EXECUTABLES "met_viewer" "Met Viewer")
    set(CPACK_CREATE_DESKTOP_LINKS "met_viewer")
else()
    # AppImage is the primary Linux artifact and is produced from the install
    # tree by linuxdeploy in CI; TGZ is a convenient CPack fallback. RPM/DEB can
    # be added by setting CPACK_GENERATOR when the build host has rpmbuild/dpkg.
    set(CPACK_GENERATOR "TGZ")
endif()

include(CPack)
