# Packaging: install rules + CPack configuration for distributable installers
# (AppImage / TGZ on Linux, NSIS on Windows). Included from the top-level
# CMakeLists.txt after every target is defined so install(TARGETS ...) resolves.
#
# Deployment of the Qt runtime is NOT done here: it is handled per-platform in CI
# by linuxdeploy (AppImage) and windeployqt (Windows), which copy the Qt shared
# libraries and plugins next to the installed executable.

include(GNUInstallDirs)

# --- Application executable ---------------------------------------------------
install(TARGETS met_viewer
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# --- Qt runtime bundling ------------------------------------------------------
# On Windows, let Qt's own deploy tooling (windeployqt) copy the required Qt
# DLLs and plugins next to the installed executable so the NSIS package is
# self-contained. On Linux the AppImage is assembled by linuxdeploy in CI, so we
# skip Qt's deploy there (NO_UNSUPPORTED_PLATFORM_ERROR keeps configure quiet).
if(WIN32)
    qt_generate_deploy_app_script(
        TARGET met_viewer
        OUTPUT_SCRIPT _met_deploy_script
        NO_UNSUPPORTED_PLATFORM_ERROR
        NO_TRANSLATIONS
    )
    install(SCRIPT "${_met_deploy_script}")
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
