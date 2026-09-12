#!/usr/bin/env bash
#
# Regenerate the screenshots embedded in docs/getting-started.md.
#
# Uses the app's headless `--grab` path to render each tutorial scene to a PNG.
# Requires a built binary. The Map scenes fetch basemap tiles from the network;
# run online for basemaps, otherwise the data still renders over the bundled
# coastlines/graticule.
#
# Runs on Linux and under Git Bash on Windows. The two differ in three places,
# all handled below: where the binary and the data file live, how the app's
# settings are isolated per shot (a throwaway XDG_CONFIG_HOME on Linux, the
# registry on Windows), and which interpreter carries Pillow for the GIF.
#
# Usage (from the repo root):
#   bash tools/gen_screenshots.sh
#   GRIB=/path/to/other.grib bash tools/gen_screenshots.sh   # override the data file
#   BIN=/path/to/met_viewer bash tools/gen_screenshots.sh    # override the binary
#
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="$REPO/docs/images"

case "$(uname -s)" in
  MINGW* | MSYS* | CYGWIN*) WINDOWS=1 ;;
  *) WINDOWS=0 ;;
esac

# ERA5 pressure-level GRIB over the US Southwest (u,v,z,t,r; ~29 levels, hourly).
if [ "$WINDOWS" = 1 ]; then
  BIN="${BIN:-$REPO/build/vs2022/viewer/app/met_viewer.exe}"
  GRIB="${GRIB:-$HOME/projects/nufall/data/validation/met/trinity_era5.grib}"
else
  BIN="${BIN:-$REPO/build/release/viewer/app/met_viewer}"
  GRIB="${GRIB:-/home/lefebvre/projects/nufall/data/validation/met/trinity_era5.grib}"
fi
# Analysis sample point inside that domain (near White Sands, NM).
AT="33.6,-106.4"

[ -x "$BIN" ] || { echo "Build first (binary not found: $BIN; set BIN=...)" >&2; exit 1; }
[ -f "$GRIB" ] || { echo "GRIB not found: $GRIB (set GRIB=...)" >&2; exit 1; }
mkdir -p "$OUT"

# Every shot starts from empty settings, so a colormap, basemap, dock layout or
# unit choice persisted by one shot never leaks into the next, and the user's
# real settings are left as they were.
if [ "$WINDOWS" = 1 ]; then
  # QSettings lives in the registry on Windows, where XDG_CONFIG_HOME means
  # nothing. The app's key is backed up once, cleared before each shot, and put
  # back on exit whatever happens. MSYS2_ARG_CONV_EXCL stops Git Bash from
  # rewriting the HKCU\... arguments as if they were file paths.
  REGKEY='HKCU\Software\met-viewer\met-viewer'
  REGBAK="$(mktemp -d)/met-viewer-settings.reg"
  reg() { MSYS2_ARG_CONV_EXCL='*' reg.exe "$@" >/dev/null 2>&1; }
  HAD_SETTINGS=0
  if reg query "$REGKEY"; then
    reg export "$REGKEY" "$(cygpath -w "$REGBAK")" /y || true
    [ -s "$REGBAK" ] || { echo "Could not back up $REGKEY; refusing to clear it" >&2; exit 1; }
    HAD_SETTINGS=1
  fi
  restore_settings() {
    reg delete "$REGKEY" /f || true
    if [ "$HAD_SETTINGS" = 1 ]; then reg import "$(cygpath -w "$REGBAK")" || true; fi
  }
  trap restore_settings EXIT
fi

# Run the app once on clean settings with the given arguments. The images are
# 1:1: without QT_ENABLE_HIGHDPI_SCALING=0 a scaled display renders them larger
# than requested (2100x1075 for 1680x860 at 125%).
launch() {
  if [ "$WINDOWS" = 1 ]; then
    reg delete "$REGKEY" /f || true
    QT_ENABLE_HIGHDPI_SCALING=0 "$BIN" "$GRIB" "$@" >/dev/null 2>&1 || true
  else
    local cfg; cfg="$(mktemp -d)"
    XDG_CONFIG_HOME="$cfg" QT_QPA_PLATFORM=xcb QT_ENABLE_HIGHDPI_SCALING=0 \
      "$BIN" "$GRIB" "$@" >/dev/null 2>&1 || true
    rm -rf "$cfg"
  fi
}

# Capture one scene.
shot() {
  local name="$1"; shift
  launch "$@" --grab "$OUT/$name.png"
  echo "  docs/images/$name.png"
}

# The GIF step needs Pillow. Each interpreter is tried rather than trusting the
# first on PATH: on Windows "python3" is usually the Microsoft Store stub, which
# exists, prints an install prompt and fails.
PY=""
for cand in python3 python py; do
  if command -v "$cand" >/dev/null 2>&1 && "$cand" -c "import PIL" >/dev/null 2>&1; then
    PY="$cand"
    break
  fi
done

# Render a sequence of time steps and assemble them into an animated GIF.
animate() {
  local name="$1"; shift          # output basename (no extension)
  local tmp; tmp="$(mktemp -d)"
  local frames=() idx frame
  for idx in "$@"; do
    frame="$tmp/f$(printf '%03d' "$idx").png"
    launch --var t --level 500 --time "$idx" --grab "$frame"
    frames+=("$frame")
  done
  if [ -n "$PY" ]; then
    "$PY" "$REPO/tools/frames_to_gif.py" "$OUT/$name.gif" 250 0.8 "${frames[@]}" >/dev/null
    echo "  docs/images/$name.gif"
  else
    echo "  (skipped $name.gif — needs Pillow: pip install --user Pillow)"
  fi
  rm -rf "$tmp"
}

echo "Rendering tutorial screenshots into docs/images/ ..."

# --- 2D Plot (§2–§3) ---
shot 01-window-overview   --var t --level 500
shot 02-plot-contours     --var t --level 500 --contours
shot 03-colormap-turbo    --var t --level 500 --colormap turbo
# Diverging map on a signed field (U-wind), which auto-centers on zero.
shot 04-colormap-rdbu     --var u --level 500 --colormap "RdBu (diverging)"
shot 05-plot-wind-barbs   --var u --level 850 --wind 1

# --- GIS Map (§4) ---
shot 06-map-osm           --var t --level 500 --map
# OpenTopoMap rather than a Carto preset: Carto now requires an API key and serves
# an "API KEY REQUIRED" watermark on every tile without one.
shot 07-map-opentopo      --var t --level 500 --map --basemap "OpenTopoMap"
shot 08-map-esri          --var t --level 500 --map --basemap "Esri World Imagery"
shot 09-map-wind-barbs    --var u --level 850 --map --wind 1
shot 10-map-streamlines   --var u --level 850 --map --wind 2

# --- Derived / time / analysis (§5–§7) ---
shot 11-derived-windspeed --var u --level 850 --map --derived 1
animate 12-time-animation 0 4 8 12 16 20 24 28 32 36 40 44   # GIF stepping through time
shot 13-cross-section     --var t --map --demo-at "$AT" --demo section
shot 14-skewt-sounding    --var t --map --demo-at "$AT" --demo sounding
shot 15-time-series       --var t --level 500 --map --demo-at "$AT" --demo series
# Point-profile panel docked beside the map with the picked site marked. Wide like
# the tiled shot, so the map and the table both have room.
shot 17-point-profile     --var t --map --demo-at "$AT" --demo point --size 1680x860

# --- Workspace (§8) ---
# Cross-section beside a skew-T, rendered wider so each pane has room (the skew-T's
# warm surface trace runs off the edge at half of the default 1280px). --map gives
# the grab extra settle time (3.5s); the base views are removed by --tile.
shot 16-tiled-layout      --var t --map --demo-at "$AT" --tile --size 1680x860

echo "Done."
