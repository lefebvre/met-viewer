# Overlay of the built-in x64-windows triplet that builds only the release
# variant of dependencies (VCPKG_BUILD_TYPE release). Used by the dist-windows
# preset in CI to roughly halve dependency build time and binary-cache size,
# since the distributed app is Release-only and never links the debug deps.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE dynamic)

set(VCPKG_BUILD_TYPE release)
