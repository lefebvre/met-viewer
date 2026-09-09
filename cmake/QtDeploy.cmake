# Qt runtime deployment for the *build* tree (Windows).
#
# vcpkg's applocal step (VCPKG_APPLOCAL_DEPS, on by default) copies every DLL an
# executable imports next to it after linking, which is why Qt6Core.dll and the
# rest of the dependency set appear beside met_viewer.exe. It does not copy Qt's
# plugins: those are loaded at runtime with LoadLibrary, so a dependency walk
# never sees them, and vcpkg's only plugin hook (plugins/qtdeploy.ps1, invoked
# from msbuild/applocal.ps1) is a Qt5-era artifact that the Qt6 port does not
# install.
#
# Missing plugins are fatal rather than degraded. vcpkg's Qt is relocatable: it
# derives its plugin directory from wherever Qt6Core.dll was loaded from, which
# resolves correctly under vcpkg_installed/<triplet>/bin but, from the applocal
# copy in build/<preset>/viewer/app, points at a Qt6/plugins that does not
# exist. With no platforms/ beside the executable either, Qt finds no platform
# plugin and QGuiApplication aborts with "This application failed to start
# because no Qt platform plugin could be initialized". It is a modal dialog, so
# a test binary launched by ctest hangs producing no output at all.
#
# windeployqt deploys the plugins an executable needs (~0.2 s once the files are
# in place), so the build tree becomes self-contained the same way
# cmake/Packaging.cmake already makes the install tree self-contained. Callers
# that only need this at install time use MET_WINDEPLOYQT directly.

if(WIN32)
    find_program(MET_WINDEPLOYQT
        NAMES windeployqt windeployqt6
        HINTS "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/tools/Qt6/bin"
              "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
        DOC "windeployqt, used to bundle the Qt runtime beside built executables")
    if(NOT MET_WINDEPLOYQT)
        message(FATAL_ERROR
            "windeployqt not found. Qt executables in the build tree would fail "
            "to start for want of a platform plugin, and installers would ship "
            "without the Qt DLLs. It comes from qtbase's `windeployqt` feature, "
            "requested for windows in vcpkg.json.")
    endif()
endif()

# met_deploy_qt_runtime(<target> [PLUGINS <qt-plugin-target>...])
#
# Copies the Qt runtime next to a built executable. PLUGINS names Qt plugin
# targets (e.g. Qt6::QMinimalIntegrationPlugin) to deploy on top of what
# windeployqt selects on its own: windeployqt deploys only the plugins it can
# see the program needs, which for platforms means qwindows and nothing else, so
# anything chosen at runtime through QT_QPA_PLATFORM has to be named here. Each
# is placed in the subdirectory its QT_PLUGIN_TYPE calls for, which is where Qt
# looks for it.
#
# No-op off Windows, where the plugins are found through the system Qt during
# development and bundled by linuxdeploy when CI assembles the AppImage.
function(met_deploy_qt_runtime target)
    if(NOT WIN32)
        return()
    endif()

    cmake_parse_arguments(PARSE_ARGV 1 arg "" "" "PLUGINS")
    if(arg_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR
            "met_deploy_qt_runtime: unexpected arguments: ${arg_UNPARSED_ARGUMENTS}")
    endif()

    # --no-translations because the app ships no translated strings, and
    # --no-compiler-runtime because a machine that just built this has the MSVC
    # runtime already; the installer path in Packaging.cmake makes its own call
    # on both. Debug/release is left to windeployqt's own detection. `--verbose
    # 0` silences the per-DLL "is up to date" report that would otherwise be
    # printed on every relink; warnings and a nonzero exit still come through.
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND "${MET_WINDEPLOYQT}"
                --verbose 0
                --no-translations
                --no-compiler-runtime
                "$<TARGET_FILE:${target}>"
        COMMENT "Deploying the Qt runtime beside ${target}"
        VERBATIM)

    foreach(plugin IN LISTS arg_PLUGINS)
        if(NOT TARGET ${plugin})
            message(FATAL_ERROR
                "met_deploy_qt_runtime: ${plugin} is not a target. Qt defines its "
                "plugin targets in the module that owns them, so the "
                "find_package(Qt6 COMPONENTS ...) that provides it must come first.")
        endif()
        get_target_property(_kind ${plugin} TYPE)
        if(NOT _kind STREQUAL "MODULE_LIBRARY")
            # A static Qt has no plugin DLL to copy; qt_import_plugins() links
            # the plugin into the executable instead.
            continue()
        endif()
        get_target_property(_plugin_type ${plugin} QT_PLUGIN_TYPE)
        if(NOT _plugin_type)
            message(FATAL_ERROR
                "met_deploy_qt_runtime: ${plugin} has no QT_PLUGIN_TYPE, so there "
                "is no way to tell which directory Qt expects to load it from.")
        endif()
        set(_dest "$<TARGET_FILE_DIR:${target}>/${_plugin_type}")
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${_dest}"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    "$<TARGET_FILE:${plugin}>" "${_dest}/"
            COMMENT "Deploying ${plugin} beside ${target}"
            VERBATIM)
    endforeach()
endfunction()
