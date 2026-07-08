#!/usr/bin/env bash
#
# Regenerate the screenshots embedded in docs/getting-started.md.
#
# Uses the app's headless `--grab` path to render each tutorial scene to a PNG.
# Requires a built binary (cmake --build --preset release). The Map scenes fetch
# basemap tiles from the network; run online for basemaps, otherwise the data
# still renders over the bundled coastlines/graticule.
#
# Usage (from the repo root):
#   bash tools/gen_screenshots.sh
#   GRIB=/path/to/other.grib bash tools/gen_screenshots.sh   # override the data file
#
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="$REPO/build/release/viewer/app/met_viewer"
OUT="$REPO/docs/images"

# ERA5 pressure-level GRIB over the US Southwest (u,v,z,t,r; ~29 levels, hourly).
GRIB="${GRIB:-/home/lefebvre/projects/nufall/data/validation/met/trinity_era5.grib}"
# Analysis sample point inside that domain (near White Sands, NM).
AT="33.6,-106.4"

[ -x "$BIN" ] || { echo "Build first: cmake --build --preset release" >&2; exit 1; }
[ -f "$GRIB" ] || { echo "GRIB not found: $GRIB (set GRIB=...)" >&2; exit 1; }
mkdir -p "$OUT"

# Capture one scene. Each run gets a throwaway QSettings home so a persisted
# colormap/basemap/layout from one shot never leaks into the next, and the real
# user config is left untouched.
shot() {
  local name="$1"; shift
  local cfg; cfg="$(mktemp -d)"
  XDG_CONFIG_HOME="$cfg" QT_QPA_PLATFORM=xcb "$BIN" "$GRIB" "$@" \
    --grab "$OUT/$name.png" >/dev/null 2>&1 || true
  rm -rf "$cfg"
  echo "  docs/images/$name.png"
}

# Render a sequence of time steps and assemble them into an animated GIF.
# The GIF step needs python3 + Pillow; without it the frames are skipped.
animate() {
  local name="$1"; shift          # output basename (no extension)
  local tmp; tmp="$(mktemp -d)"
  local frames=() idx cfg
  for idx in "$@"; do
    cfg="$(mktemp -d)"
    XDG_CONFIG_HOME="$cfg" QT_QPA_PLATFORM=xcb "$BIN" "$GRIB" --var t --level 500 \
      --time "$idx" --grab "$tmp/f$(printf '%03d' "$idx").png" >/dev/null 2>&1 || true
    rm -rf "$cfg"
    frames+=("$tmp/f$(printf '%03d' "$idx").png")
  done
  if python3 -c "import PIL" >/dev/null 2>&1; then
    python3 "$REPO/tools/frames_to_gif.py" "$OUT/$name.gif" 250 0.8 "${frames[@]}" >/dev/null
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
shot 07-map-carto-dark    --var t --level 500 --map --basemap "Carto Dark"
shot 08-map-esri          --var t --level 500 --map --basemap "Esri World Imagery"
shot 09-map-wind-barbs    --var u --level 850 --map --wind 1
shot 10-map-streamlines   --var u --level 850 --map --wind 2

# --- Derived / time / analysis (§5–§7) ---
shot 11-derived-windspeed --var u --level 850 --map --derived 1
animate 12-time-animation 0 4 8 12 16 20 24 28 32 36 40 44   # GIF stepping through time
shot 13-cross-section     --var t --map --demo-at "$AT" --demo section
shot 14-skewt-sounding    --var t --map --demo-at "$AT" --demo sounding
shot 15-time-series       --var t --level 500 --map --demo-at "$AT" --demo series

# --- Workspace (§8) ---
# Cross-section beside a skew-T, rendered wider so each pane has room (the skew-T's
# warm surface trace runs off the edge at half of the default 1280px). --map gives
# the grab extra settle time (3.5s); the base views are removed by --tile.
shot 16-tiled-layout      --var t --map --demo-at "$AT" --tile --size 1680x860

echo "Done."
