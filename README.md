# met-viewer

C++20 desktop application for viewing and analyzing meteorological data (GRIB1/2, NetCDF4/CF, NOAA ARL). New here? Start with the [Getting Started tutorial](docs/getting-started.md). See [Design.md](Design.md) for architecture and roadmap.

## System prerequisites (RHEL/Alma 10)

vcpkg builds Qt6, OpenSSL, and others from source, which need toolchain and X11/XCB
development packages from the system package manager (vcpkg does not provide these):

```sh
# Qt6 X11/EGL/OpenGL stack + build tools
sudo dnf install -y libxcb-devel libX11-devel mesa-libGLU-devel libXrender-devel \
  libXi-devel libxkbcommon-devel libxkbcommon-x11-devel mesa-libEGL-devel \
  gperf fontconfig-devel freetype-devel

# XCB utility libs required by Qt's xcb platform plugin
sudo dnf install -y xcb-util-cursor-devel xcb-util-wm-devel xcb-util-devel \
  xcb-util-image-devel xcb-util-keysyms-devel xcb-util-renderutil-devel

# Perl core modules required by OpenSSL's Configure script
sudo dnf install -y perl-IPC-Cmd perl-FindBin perl-Text-Template perl-Time-Piece \
  perl-Unicode-Normalize perl-Test-Harness perl-Module-Loaded perl-ExtUtils-MakeMaker \
  perl-Sys-Hostname

# autotools (required to build libb2, a Qt dependency)
sudo dnf install -y autoconf automake libtool autoconf-archive
```

## Build

Dependencies are managed via a vcpkg manifest, checked in as a submodule.

```sh
git submodule update --init --recursive   # first time only

cmake --preset release
cmake --build --preset release
ctest --preset release
./build/release/viewer/app/met_viewer
```

On Wayland sessions, force the X11 backend (the vcpkg Qt build ships the `xcb`
platform plugin, not `wayland`): `QT_QPA_PLATFORM=xcb ./build/release/viewer/app/met_viewer`.

Once it's running, the [Getting Started tutorial](docs/getting-started.md) walks
through opening data, colormaps, the GIS map, wind overlays, cross-sections,
soundings, time series, and animation.

Other presets: `debug`, `asan` (address/UB sanitizers **plus `-Werror`** — run before
committing; CI runs it too). The sanitizer build needs the runtime libraries, which
GCC does not install with the compiler:

```sh
sudo dnf install -y libasan libubsan
```

Pass `--verbose <level>` (`trace|debug|info|warn|error|off`) to see diagnostics on
stderr — which reader claimed a file, why a slab was skipped, why a tile failed.

## Formatting

Style is enforced by `.clang-format` and gated in CI:

```sh
cmake --build build/release --target format         # rewrite sources in place
cmake --build build/release --target format-check   # report + fail, what CI runs
```

Use **clang-format 21.1.8** — the version the tree is formatted with, recorded in
[`cmake/Format.cmake`](cmake/Format.cmake) and installed by CI. Output is not
stable across major releases, so a different one will flag differences that are
the tool's rather than yours. If your distro ships another version:

```sh
pip install clang-format==21.1.8
```

The CMake targets are created only when clang-format is found, so its absence
never blocks a build. Two hand-aligned regions (the marching-squares dispatch
table, the tile-URL table) are fenced with `// clang-format off` and explain why
in place.

The first configure builds Qt and other dependencies from source via vcpkg; expect this to take a while.

## Installers

Tagged releases (`v*`) publish downloadable installers via GitHub Actions
(see [`.github/workflows/release.yml`](.github/workflows/release.yml)):

- **Linux** — a self-contained `.AppImage` built on AlmaLinux 9, so it runs on
  RHEL/Alma/Rocky 9 and 10 (glibc ≥ 2.34).
- **Windows** — an NSIS `.exe` installer.

Releases are currently **unsigned**: Windows SmartScreen may warn ("More info →
Run anyway"), and Linux may require `chmod +x` on the AppImage.

The distributable builds link Qt **dynamically** (LGPL-friendly) via dedicated
presets that CI uses:

```sh
cmake --preset dist-linux     # x64-linux-dynamic; AppImage source tree
cmake --build --preset dist-linux
```

To produce the local packages CPack knows about (a `.tar.gz` on Linux, the NSIS
installer on Windows):

```sh
cd build/dist-linux && cpack        # or: cpack -G TGZ
```

The AppImage itself is assembled by `linuxdeploy` in CI; `dist-windows` bundles
the Qt runtime via `windeployqt`. The app icon set lives in
[`resources/icons/`](resources/icons/); `tools/gen_app_icons.py` regenerates the
Windows `.ico` from the largest app PNG.

## CI

Every push to `master` and every pull request is built and tested on Linux
(AlmaLinux 9 container) and Windows via
[`.github/workflows/ci.yml`](.github/workflows/ci.yml), including a headless
render smoke test on Linux. vcpkg dependencies are cached in the GitHub Actions
cache so Qt is compiled from source only once.
