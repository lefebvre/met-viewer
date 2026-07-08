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

# autoconf-archive (required to build libb2, a Qt dependency)
sudo dnf install -y autoconf-archive
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

Other presets: `debug`, `asan` (address/UB sanitizers — run before committing).

The first configure builds Qt and other dependencies from source via vcpkg; expect this to take a while.
