# met-viewer — C++20 Meteorological Data Viewer & Analysis Tool

## Context

Greenfield project in an empty workspace (`/home/lefebvre/projects/met-viewer`). Goal: a desktop application for viewing and analyzing gridded meteorological data (ERA5, NOAA products, GRIB2, ARL packed), with two view modes — plain 2D scalar plots, and GIS renderings draped semi-transparently over basemap tiles — plus wind vector fields, vertical cross-sections/soundings, and time series/animation.

**User decisions (fixed):** Qt6 Widgets desktop app · basemap = XYZ raster tiles (OSM/Carto/Esri) with data warped to Web Mercator · analysis = wind vectors, cross-sections, time series/animation (derived quantities like vorticity deferred) · dependencies via **vcpkg manifest mode**.

**Environment (verified):** GCC 14.3.1 (full C++20), CMake 3.31.8, Ninja 1.11.1. System netcdf/hdf5-devel exist but we isolate against them via the vcpkg toolchain (CONFIG-mode `find_package` only). No vcpkg yet — bootstrap is Milestone 0.

**Format scope (answers "other formats?"):** ERA5 and "NOAA" are products, not formats — they arrive as GRIB1/2 or NetCDF4/CF. v1 readers: **GRIB1/2** (ecCodes), **NetCDF4/HDF5 with CF conventions** (netcdf-c; covers ERA5 native downloads), **ARL packed** (custom reader — no C++ library exists). Deferred: GeoTIFF/GDAL (huge dep, rare for met grids; add post-v1 behind a vcpkg feature), Zarr/ARCO-ERA5 (remote object storage is its own project), OPeNDAP (nearly free later via netcdf-c DAP — key `open()` on a string, not just a path). Out of scope: BUFR (non-gridded observation data; different data model entirely).

## Architecture

Strict layering; Qt only in the top layers so core/readers/analysis are testable headless:

```
viewer/
├── core/       met_core     (PROJ, fmt)      grid.h field.h catalog.h crs.h units.h timeaxis.h geo.h log.h
├── readers/    met_readers  (+eccodes, netcdf-c)  ireader.h detect.cpp grib/ netcdf/ arl/
├── analysis/   met_analysis (met_core)       sample, crosssection, wind, streamline
├── render/     met_render   (Qt6::Gui ONLY)  colormap, warp, contour, tilemath, vectoroverlay
└── app/        met_viewer   (Widgets/Network/OpenGLWidgets)
                mainwindow mapview plotview2d tilelayer layermodel datasetdock
                timecontroller colorbarwidget crosssection/skewt/timeseries tabs
                jobs (pooled work) extractions (slab reads + analysis, Qt-free)
resources/      Natural Earth coastlines/borders as compact binary polylines, icons
tools/          gen_colormaps.py  ne_convert.py  make_fixtures.sh  arl_writer.py
tests/          GoogleTest, fixtures generated at build time
```

### Core data model (the load-bearing abstraction)

```cpp
using GridDef = std::variant<RegularLatLonGrid,   // ERA5 NetCDF, GRIB regular_ll
                             GaussianGrid,        // ERA5 GRIB regular gaussian
                             ProjectedGrid>;      // GRIB Lambert/polar-stereo, ALL ARL (proj4 string + x0,y0,dx,dy)
```
Every `GridDef` provides `latlonToIndex()` / `indexToLatLon()` — analytic for the first two, cached PROJ transform for `ProjectedGrid`. This single choke point makes all formats uniform to warp/sampling/contours. Reduced-gaussian GRIB: expand via ecCodes or document "download regular grids" (post-v1).

- `FieldKey` = {varName, VerticalLevel, validTime, ensemble member}; `Field2D` = {GridDef, `vector<float>` row-major, meta, **`id`**}. **Missing values normalized to NaN at decode time**, everywhere. `id` is a process-unique counter copied with the values: views cache expensive derived products (warped raster, isolines, GPU texture) and key them on it, because the field's *address* can be reused by the allocator and would silently serve a stale raster.
- `DatasetCatalog`: per variable, sorted level/time axes + sparse map → opaque `RecordHandle` (GRIB byte offset / NetCDF (varid,indices) / ARL record number). **Open reads metadata only; `readField()` decodes one 2D slab lazily** — this is what makes 500-message GRIB files and animation tractable.
- Units: small hardcoded table (K↔°C, Pa↔hPa, m/s↔kt, m²/s²↔gpm, …) — no UDUNITS dep. Two traps worth stating: geopotential (m²/s², what ERA5's `z` carries) is **not** geopotential height and converts through g = 9.80665; and a dimensionless `"1"` is deliberately not treated as a mixing ratio, since masks and fractions use it too.
- Logging: `core/log.h`, a severity-filtered stderr channel (`--verbose <level>`), so decode/IO failures that a `catch` swallows still leave a trace. Deliberately not spdlog — fmt is already a dependency and this is ~60 lines.
- PROJ 9.x wrapped RAII, thread_local contexts, batched `proj_trans_generic` only (never per-point in hot loops).

### Reader abstraction

`IFormatReader::probe(first 4KB, path) → 0–100` confidence + `open() → unique_ptr<IDataset>`; compile-time static registry (no dlopen). Magic bytes: `GRIB` (scan window — WMO preambles exist), `\x89HDF`, `CDF\x01/\x02`; ARL heuristic (14 ASCII digits + `INDX` + parseable 50-byte label, score 80). Thread-safety encoded per reader: GRIB = fresh handle + per-call `FILE*`; ARL = fully reentrant, int64 offsets throughout (multi-day files exceed 2 GB and `long` is 32-bit on Windows); netcdf-c = **one process-wide mutex**, not per-instance — the HDF5 we link has no `H5_HAVE_THREADSAFE`, so no two threads may be inside the library at once *on any file*, and `MultiDataset` deliberately reads different sources in parallel.

`RegularLatLonGrid` is a uniform-spacing model, so the CF reader **verifies both coordinate axes are evenly spaced** and rejects the file otherwise. A Gaussian latitude axis (common when ERA5 GRIB is converted to NetCDF) would otherwise be silently mis-georeferenced with no symptom but values in the wrong place.

### Rendering

- **GIS view**: CPU warp into one viewport-sized `QImage` composited over tiles via QPainter. Per-pixel mapping: closed-form inverse-mercator for lat-lon grids (no PROJ in the hot loop), a batched PROJ transform for projected grids. **Multi-threaded**: output rows are partitioned into chunks run on `std::thread`s, and each chunk does its *own* PROJ batch (Crs caches a `PJ` per thread) — the batch is ~2/3 of the cost, so hoisting it out of the parallel region capped the speedup at ~1.5×. Keeping the buffers per chunk also drops peak allocation from two viewport-sized double arrays (~33 MB at 1080p) to a slice. **Pan blits the cached raster at an offset** and re-warps once on mouse release; zoom/resize/field/colormap changes re-warp immediately. Budget (both enforced by benchmark tests): 1080p ERA5 warp < 15 ms single-threaded; 1080p HRRR-shaped Lambert ~27 ms multi-threaded. The warp is still synchronous inside `paintGL` — an async warp job is the next lever if that stops being enough.
- **2D plot view**: draws the field in **flat grid-index space** (`fieldToImage` + `indexToScreen`), so the raster and its isolines share one mapping. The probe, wind barbs and axes all route through that same index mapping and derive geography from the grid, never from a linear interpolation over the lat/lon bounding box: for a projected grid that box is an *envelope* that bulges past the data, which put an HRRR domain's corners 11° of longitude away from what was drawn. Where the axes cannot honestly carry degrees (projected/ARL grids) they are labelled with grid indices and a true lat/lon graticule is traced through the projection on top.
- **QPainter first, OpenGL later**: pure QPainter through v1; M7 adds a `QOpenGLWidget` fast path for animation only (R32F field texture + 256×1 LUT shader; per-frame cost = one upload).
- **Colormaps**: `tools/gen_colormaps.py` (matplotlib/cmocean, run once, output committed as `colormaps_data.h`): viridis, turbo, magma, cividis, cmocean thermal/haline/balance + stepped NWS reflectivity, precip, temperature-anchored-at-0°C. Linear/log normalization, discrete levels, under/over/NaN colors. Colorbar widget renders from the same object.
- **Contours**: hand-written marching squares (~250 lines) on the native grid → polylines in index space, NaN-aware; superb unit-test target. **Not blocky**: crossing points are linearly interpolated along cell edges, so output follows the field smoothly (piecewise-linear at grid resolution, like matplotlib's contour). An optional Chaikin corner-cutting pass smooths coarse grids further.
- **Wind**: U/V pairing table (`u/v`, `10u/10v`, `ugrd/vgrd`, ARL `UWND/VWND`, CF names). **Trap: rotate grid-relative winds to earth-relative on projected grids** (GRIB resolutionAndComponentFlags). Two subtleties, both learned the hard way: the convergence angle is the bearing of the **projection's** +y axis, not of increasing row index — those differ by 180° whenever a message scans north-to-south (`jScansPositively = 0`), which silently reverses every vector; and evaluating it per cell costs two PROJ inverse transforms per grid point (~340 ms on HRRR), so it is sampled on a coarse lattice and bilinearly interpolated as a (cos, sin) pair, which is ~16 ms and agrees to better than 0.1°. Barbs decimated to ~40 px screen lattice, knots quantization; streamlines = Jobard–Lefer + RK4.
- **Basemap**: XYZ presets — OSM standard, Carto light/dark, Esri World Imagery (satellite), and terrain presets OpenTopoMap and Esri World Shaded Relief (standard OSM tiles carry no terrain shading, so terrain is its own preset). Plus a **"Custom URL…" entry** so any XYZ source the user has rights to can be added; the template is validated (http(s) + `{z}/{x}/{y}`) so a pasted `file://` cannot turn the box into a local-file reader, and it is persisted in `QSettings`. Google hybrid tiles are technically reachable through the custom entry but Google's ToS prohibits direct XYZ tile access outside their SDK — not shipped as a preset; Esri World Imagery is the licensed equivalent. `QNetworkAccessManager` + `QNetworkDiskCache` (512 MB), **identifying User-Agent with real contact info (OSM policy blocks Qt's default, and placeholder domains get blocked too)**, ≤6 in-flight, corner attribution (required for OSM), and a **failed-tile budget**: after 3 consecutive failures a tile stops being re-requested until the source changes, since repaints happen on every mouse-move and would otherwise become a steady request stream. Coastlines/borders: Natural Earth 50m pre-converted offline to a dumb float32 polyline binary (~1–2 MB committed) — avoids GDAL entirely. Graticule computed from zoom.

### App shell & threading

Dock layout: dataset tree (left), layer list w/ opacity+colormap (left-bottom), inspector + level selector (right), time controller (bottom), tabbed center (map / 2D plot / section / sounding / time-series tabs). Status-bar probe = one bilinear sample of the top field layer. Mode toolbar: Pan / Probe / Cross-section (click vertices) / Sounding / Time series.

One shared `QThreadPool`; DecodeJob/SectionJob with generation counters; results posted via queued `invokeMethod` to a **`QPointer`** context (a raw pointer would be dereferenced to find the receiver's thread even after the context died), stale generations dropped. Anything that reads slabs runs there: the multi-slab reads and the sounding/cross-section/time-series extractions live in `app/extractions.h` as free functions over an `IDataset` — no window state, no widgets, unit-testable — and the wind overlay's U/V decode is a pooled job too, with same-key requests coalescing onto one decode (the overlay and a derived quantity both want the pair from a single user action). App-level LRU `FieldCache` — **size is a user setting** (Preferences dialog, default 1 GB, persisted in `QSettings`) — plus a 3–5-timestep decode-ahead prefetcher for animation. Cross-section = great-circle path × log-p pressure axis, same colormap engine. Sounding = **skew-T log-p diagram**: skewed temperature axis (45° isotherms), log-p vertical, T and Td traces, with background isotherms, dry adiabats, moist adiabats, and mixing-ratio lines drawn as precomputed QPainter paths; wind barbs along the right margin when U/V available.

## Dependencies & build

Naming: all CMake targets use underscores — `met_core`, `met_readers`, `met_analysis`, `met_render`, `met_viewer` (executable). The one forced exception is the vcpkg manifest `name`, which must be `met-viewer` (vcpkg requires lowercase-hyphen names and rejects underscores).

`vcpkg.json` (submodule + pinned baseline): **qtbase** (default-features off; widgets, gui, network, opengl, png, jpeg, concurrent, openssl), **eccodes**, **netcdf-c**, **hdf5**, **proj**, **fmt**, **spdlog**, **gtest**. No GDAL in v1. First build is hours (Qt) — M0 absorbs it.

CMake: presets `debug` / `release` / `asan` (`-fsanitize=address,undefined` is the CI-less safety net), `met_options` interface target for C++20 + warnings, static libs enforce layering via link privacy, `compile_commands.json` on.

## Testing

GoogleTest (+ GMock) via vcpkg, `gtest_discover_tests` + CTest; ritual: `cmake --preset asan && ctest`. Fixtures generated by `tools/make_fixtures.sh`:
- GRIB: ecCodes samples + `grib_set` to stamp tiny grids with known analytic values (incl. a Lambert message).
- NetCDF: committed human-readable `.cdl` (ERA5-shaped: packed shorts w/ scale/offset, N→S lats, 0–360 lons, `_FillValue`) → `ncgen` at build time.
- ARL: (a) round-trip vs our own `arl_writer.py`, (b) slice of a real NOAA/HYSPLIT sample with expected values from HYSPLIT `chk_data` hardcoded — proves the *understanding*, not just self-consistency.
- Warp/colormap: golden-image PNGs of analytic fields per grid type, per-pixel tolerance. **Headless-render note (learned at M0):** the vcpkg static qtbase build ships the `xcb`, `minimal`, `linuxfb`, and `vnc` platform plugins but **not** `offscreen`, and static Qt only auto-imports the default (`xcb`) plugin. For headless render tests, add `qt_import_plugins(met_render_tests INCLUDE Qt6::QMinimalIntegrationPlugin)` and run with `QT_QPA_PLATFORM=minimal` (QPainter-to-QImage needs no window), or run xcb under `xvfb-run`. Do not rely on `QT_QPA_PLATFORM=offscreen`.
- Contour/sampling/tilemath/units: analytic unit tests.

## Roadmap (each milestone runnable + demo criterion)

- **M0 — Walking skeleton**: vcpkg bootstrap, manifest, presets, all targets compile, empty QMainWindow, one passing test. *Done: window opens, ctest green.*
- **M1 — GRIB2 → 2D plot end-to-end**: GRIB reader (regular_ll), catalog + dataset dock, colormaps (viridis/turbo), PlotView2D + colorbar, probe, basic DecodeJob. *Done: open GFS/ERA5-GRIB, click "t @ 500 hPa", correct colormapped plot with probe readout.*
- **M2 — NetCDF/CF + axes UX**: ERA5 NetCDF reader (packed shorts, N→S, 0–360), level selector + time slider, GRIB1, contours. *Done: same ERA5 field identical from GRIB and NetCDF; scrub levels/times.*
- **M3 — GIS map view** *(risk #2 burn-down)*: tiles (fetch/cache/attribution/pan/zoom), regular-grid warp + opacity, graticule, Natural Earth overlays, **warp benchmark test as regression tripwire**, hidden playback-benchmark action. *Done: t2m over OSM at interactive pan/zoom, warp under budget.*
- **M4 — Projected grids + ARL** *(risk #1)*: PROJ integration, GRIB Lambert + gaussian, **ARL reader** (spike first: standalone decode of one NOAA record validated against `chk_data` before any integration; translate ARL grid encoding → proj4 `lcc/stere/merc`, verify by round-tripping grid corners; crib HYSPLIT `PAKINP` Fortran + Python readers as references). *Done: HRRR-style Lambert file and HYSPLIT ARL file render correctly over basemap, values match chk_data.*
- **M5 — Wind**: pairing, barbs/arrows both views, grid-relative rotation verified on Lambert, streamlines. *Done: 300 hPa jet looks right.*
- **M6 — Analysis tools**: cross-section tab, **skew-T log-p sounding tab** (background adiabat/isotherm/mixing-ratio lines, T/Td traces, margin barbs), time-series extraction. *Done: section through a front shows coherent thermal structure; a sounding renders as a proper skew-T.*
- **M7 — Animation & polish** *(risk #3)* — DONE: FieldCache (LRU, user-set byte budget, decode-ahead prefetch of upcoming timesteps) + in-app playback (TimeController play/pause + fps QTimer stepping the time axis, cache-backed so frames are instant) + QSettings persistence (window state, colormap/basemap/opacity/overlays, cache size, fps) + Preferences dialog. MP4/GIF export deferred (the `--grab` frame capture exists for stills).
- **GPU fast path** (post-v1) — IMPLEMENTED, opt-in: MapView is a `QOpenGLWidget`; a fragment shader inverts Web Mercator and colormaps a regular lat/lon field per pixel (the CPU pre-applies the colormap to the small grid → RGBA8 texture; the GPU does the expensive warp + hardware bilinear). Needs qtbase built with the `egl` feature (added to vcpkg.json) so the xcb plugin can create a GL context. **Defaults OFF** ("GPU render (experimental)" toggle): on the dev box's AMD Raphael iGPU (radeonsi, Mesa 25.2.7, GL 4.6 Core) the shader samples a correctly-uploaded RGBA8 texture wrongly in part of the field (a driver artifact — verified the CPU-side texel bytes are correct, ruled out float textures, LUT, discard, MSAA, VAO, pixel-store, sampler objects, upload sync). The CPU warp (~38 ms/1080p + FieldCache) is the robust default and renders correctly inside the same QOpenGLWidget.

## Top risks

1. **ARL reader correctness** — under-documented packing (50-byte ASCII labels, `INDX` grid records, 1-byte scaled-difference packing). Mitigation: M4 week-1 spike against real NOAA data + `chk_data`, dual-pronged tests, reference decoders.
2. **Interactive warp of arbitrary grids** — mitigation: mapping design (analytic inverses, per-chunk batched PROJ, pan = blit the cached raster at an offset) + benchmark tests on *both* paths that fail on regression. The projected path is the expensive one and was untested for performance for a while; the regular-grid tripwire could not have caught it.
3. **Animation performance** — GRIB decode 50–200 ms/msg. Mitigation: LRU cache + decode-ahead + GL LUT path; measure from M3 via hidden benchmark; fallback = pre-decode range with progress bar (cache-compatible).

## Verification

- Per milestone: run the demo criterion manually (`./build/release/met_viewer` with a real GFS/ERA5/HYSPLIT file).
- Every commit and every CI run: `ctest --preset asan` (unit + golden-image + warp-benchmark tests). CI builds `dist-linux` with `-DMET_WERROR=ON` and then re-runs the suite under address/undefined sanitizers — the warning set is only a gate if something fails on it. The sanitizer job needs `libasan`/`libubsan` installed; GCC ships only the compiler half.
- Data correctness cross-checks: probe values vs `grib_get_data` / `ncdump` / HYSPLIT `chk_data` for the same point.
