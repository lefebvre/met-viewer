# Overlay of the community x64-linux-dynamic triplet that builds only the release
# variant of dependencies (VCPKG_BUILD_TYPE release). Used by the dist-linux
# preset in CI to roughly halve dependency build time and binary-cache size,
# since the distributed app is Release-only and never links the debug deps.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE dynamic)

set(VCPKG_CMAKE_SYSTEM_NAME Linux)

set(VCPKG_FIXUP_ELF_RPATH ON)

set(VCPKG_BUILD_TYPE release)
