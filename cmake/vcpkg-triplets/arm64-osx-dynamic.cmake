# Overlay of the community arm64-osx-dynamic triplet that builds only the release
# variant of dependencies (VCPKG_BUILD_TYPE release), as the Linux and Windows
# overlays do. Used by the dist-macos preset for the Apple Silicon app bundle.
set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE dynamic)

set(VCPKG_CMAKE_SYSTEM_NAME Darwin)
set(VCPKG_OSX_ARCHITECTURES arm64)

# The oldest macOS the bundle runs on, for every dependency as well as the app
# (dist-macos sets the same for the app itself). Left unset, each library targets
# the macOS of the machine that built it, so a bundle built on the CI runner
# would refuse to load on anything older than the runner. 13 is the oldest
# release Qt 6.11 supports.
set(VCPKG_OSX_DEPLOYMENT_TARGET 13.0)

# Libraries are identified as @rpath/<name>.dylib rather than by their absolute
# path in vcpkg_installed, which macdeployqt then rewrites to the bundle's
# Frameworks directory.
set(VCPKG_FIXUP_MACHO_RPATH ON)

set(VCPKG_BUILD_TYPE release)
