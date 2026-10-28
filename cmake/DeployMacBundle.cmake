# Install-time script (install(SCRIPT) from cmake/Packaging.cmake): makes the
# installed app bundle self-contained and signs it. Runs under both
# `cmake --install` and cpack, so the DMG and an installed tree are the same.
#
# Expects, from the install(CODE) just before it:
#   MET_APP_BUNDLE          the installed .app
#   MET_MACDEPLOYQT         the macdeployqt executable
#   MET_DEPLOY_LIBPATH      where the app's @rpath libraries live (vcpkg's lib/)
#   MET_CODESIGN_IDENTITY   "-" for an ad-hoc signature, else a signing identity

if(NOT IS_DIRECTORY "${MET_APP_BUNDLE}")
    message(FATAL_ERROR "app bundle not found at ${MET_APP_BUNDLE}")
endif()

# macdeployqt copies Qt, its plugins and every library the executable links into
# Contents/Frameworks and Contents/PlugIns, and rewrites each reference to point
# there. It exits 0 even when it could not find a library and only says so in its
# log, which is how a bundle missing a dependency would otherwise ship.
message(STATUS "Deploying the Qt runtime into ${MET_APP_BUNDLE}")
execute_process(
    COMMAND "${MET_MACDEPLOYQT}" "${MET_APP_BUNDLE}" -verbose=1
            "-libpath=${MET_DEPLOY_LIBPATH}"
    RESULT_VARIABLE _result
    OUTPUT_VARIABLE _log
    ERROR_VARIABLE _log)
message("${_log}")
if(NOT _result EQUAL 0 OR _log MATCHES "ERROR:")
    message(FATAL_ERROR "macdeployqt failed (exit ${_result}); see its log above")
endif()

# Every Mach-O file in the bundle may reference only the bundle itself and the
# OS. An absolute path into vcpkg_installed would load on the machine that built
# the bundle and nowhere else.
file(GLOB_RECURSE _files LIST_DIRECTORIES false
     "${MET_APP_BUNDLE}/Contents/MacOS/*"
     "${MET_APP_BUNDLE}/Contents/Frameworks/*"
     "${MET_APP_BUNDLE}/Contents/PlugIns/*")
set(_leaks "")
foreach(_file IN LISTS _files)
    if(IS_SYMLINK "${_file}")
        continue()
    endif()
    execute_process(COMMAND file -b "${_file}" OUTPUT_VARIABLE _kind)
    if(NOT _kind MATCHES "Mach-O")
        continue()
    endif()
    execute_process(COMMAND otool -L "${_file}" OUTPUT_VARIABLE _deps)
    string(REPLACE "\n" ";" _deps "${_deps}")
    foreach(_dep IN LISTS _deps)
        # Dependency lines are indented; the first line names the file itself.
        if(NOT _dep MATCHES "^[ \t]+(/[^ ]+)")
            continue()
        endif()
        set(_path "${CMAKE_MATCH_1}")
        if(NOT _path MATCHES "^/(System|usr/lib)/")
            string(APPEND _leaks "\n  ${_file} -> ${_path}")
        endif()
    endforeach()
endforeach()
if(_leaks)
    message(FATAL_ERROR "the bundle still links libraries outside itself:${_leaks}")
endif()

# Sign last: the signature seals every file in the bundle. --deep signs the
# frameworks and plugins inside it too. A real identity also gets the hardened
# runtime and a secure timestamp, both of which notarization requires.
set(_sign --force --deep --sign "${MET_CODESIGN_IDENTITY}")
if(NOT MET_CODESIGN_IDENTITY STREQUAL "-")
    list(APPEND _sign --options runtime --timestamp)
endif()
message(STATUS "Signing ${MET_APP_BUNDLE} (identity: ${MET_CODESIGN_IDENTITY})")
execute_process(COMMAND codesign ${_sign} "${MET_APP_BUNDLE}" RESULT_VARIABLE _result)
if(NOT _result EQUAL 0)
    message(FATAL_ERROR "codesign failed (${_result})")
endif()
execute_process(COMMAND codesign --verify --deep --strict --verbose=2 "${MET_APP_BUNDLE}"
                RESULT_VARIABLE _result)
if(NOT _result EQUAL 0)
    message(FATAL_ERROR "the signed bundle does not verify (${_result})")
endif()
