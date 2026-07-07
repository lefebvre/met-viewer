#!/usr/bin/env bash
# Regenerates committed test fixtures. Requires a configured build tree so the
# vcpkg-built ecCodes (headers, libs, definitions, samples) is available.
#
# Usage:  tools/make_fixtures.sh [build-preset-dir]
#   build-preset-dir defaults to build/release
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${1:-$ROOT/build/release}"

# vcpkg_installed may live at the source root (shared) or under the build tree.
if [[ -d "$ROOT/vcpkg_installed/x64-linux/include" ]]; then
    VI="$ROOT/vcpkg_installed/x64-linux"
elif [[ -d "$BUILD/vcpkg_installed/x64-linux/include" ]]; then
    VI="$BUILD/vcpkg_installed/x64-linux"
else
    echo "error: vcpkg install not found — configure a preset first" >&2
    exit 1
fi

export ECCODES_DEFINITION_PATH="$VI/share/eccodes/definitions"
export ECCODES_SAMPLES_PATH="$VI/share/eccodes/samples"

FIXDIR="$ROOT/tests/fixtures"
mkdir -p "$FIXDIR"

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

echo "compiling make_grib_fixture..."
g++ "$ROOT/tools/make_grib_fixture.c" -I"$VI/include" -L"$VI/lib" \
    -leccodes -leccodes_memfs -lopenjp2 -laec -lpng16 -lz -lm \
    -Wl,-rpath,"$VI/lib" -o "$TMP/make_grib_fixture"

echo "generating GRIB2 regular_ll fixture..."
"$TMP/make_grib_fixture" "$FIXDIR/regular_ll_t500.grib2" GRIB2

echo "generating GRIB1 regular_ll fixture..."
"$TMP/make_grib_fixture" "$FIXDIR/regular_ll_t500.grib1" GRIB1

echo "compiling + generating Lambert GRIB2 fixture..."
g++ "$ROOT/tools/make_lambert_fixture.c" -I"$VI/include" -L"$VI/lib" \
    -leccodes -leccodes_memfs -lopenjp2 -laec -lpng16 -lz -lm \
    -Wl,-rpath,"$VI/lib" -o "$TMP/make_lambert_fixture"
"$TMP/make_lambert_fixture" "$FIXDIR/lambert_sfc.grib2"

echo "compiling + generating wind (u/v) GRIB2 fixture..."
g++ "$ROOT/tools/make_wind_fixture.c" -I"$VI/include" -L"$VI/lib" \
    -leccodes -leccodes_memfs -lopenjp2 -laec -lpng16 -lz -lm \
    -Wl,-rpath,"$VI/lib" -o "$TMP/make_wind_fixture"
"$TMP/make_wind_fixture" "$FIXDIR/wind_uv_850.grib2"

echo "compiling make_netcdf_fixture..."
# Static libnetcdf drags in HDF5, szip/aec, and curl (DAP) with its own chain.
g++ "$ROOT/tools/make_netcdf_fixture.c" -I"$VI/include" -L"$VI/lib" \
    -lnetcdf -lhdf5_hl -lhdf5 -lsz -laec \
    -lcurl -lssl -lcrypto -lbrotlidec -lbrotlicommon -lbz2 -llzma -lz -ltinyxml2 -lzstd \
    -lm -ldl -lpthread \
    -Wl,-rpath,"$VI/lib" -o "$TMP/make_netcdf_fixture"

echo "generating ERA5-shaped NetCDF fixture..."
"$TMP/make_netcdf_fixture" "$FIXDIR/era5_t_pl.nc"

echo "generating synthetic ARL fixture..."
python3 "$ROOT/tools/make_arl_fixture.py" "$FIXDIR/small_latlon.arl"

echo "done. fixtures in $FIXDIR:"
ls -l "$FIXDIR"
