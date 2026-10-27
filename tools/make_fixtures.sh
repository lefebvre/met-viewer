#!/usr/bin/env bash
# Regenerates committed test fixtures. Requires a configured build tree so the
# vcpkg-built ecCodes (headers, libs, definitions, samples) is available.
#
# Usage:  tools/make_fixtures.sh [build-preset-dir]
#   build-preset-dir defaults to build/release
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${1:-$ROOT/build/release}"

# vcpkg_installed may live at the source root (shared) or under the build tree,
# under whichever Linux triplet the configured preset used: x64-linux for the
# debug/release/asan presets, x64-linux-dynamic for the dist ones.
VI=""
for base in "$ROOT/vcpkg_installed" "$BUILD/vcpkg_installed"; do
    for triplet in x64-linux x64-linux-dynamic; do
        if [[ -z "$VI" && -d "$base/$triplet/include" ]]; then VI="$base/$triplet"; fi
    done
done
if [[ -z "$VI" ]]; then
    echo "error: no x64-linux or x64-linux-dynamic vcpkg install found — configure a preset first" >&2
    exit 1
fi
echo "using $VI"

export ECCODES_DEFINITION_PATH="$VI/share/eccodes/definitions"
export ECCODES_SAMPLES_PATH="$VI/share/eccodes/samples"

FIXDIR="$ROOT/tests/fixtures"
mkdir -p "$FIXDIR"

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

echo "compiling make_grib_fixture..."
g++ "$ROOT/tools/make_grib_fixture.c" -I"$VI/include" -L"$VI/lib" \
    -leccodes -leccodes_memfs -lopenjp2 -lpng16 -lz -lm \
    -Wl,-rpath,"$VI/lib" -o "$TMP/make_grib_fixture"

echo "generating GRIB2 regular_ll fixture..."
"$TMP/make_grib_fixture" "$FIXDIR/regular_ll_t500.grib2" GRIB2

echo "generating GRIB1 regular_ll fixture..."
"$TMP/make_grib_fixture" "$FIXDIR/regular_ll_t500.grib1" GRIB1

echo "compiling + generating Lambert GRIB2 fixture..."
g++ "$ROOT/tools/make_lambert_fixture.c" -I"$VI/include" -L"$VI/lib" \
    -leccodes -leccodes_memfs -lopenjp2 -lpng16 -lz -lm \
    -Wl,-rpath,"$VI/lib" -o "$TMP/make_lambert_fixture"
"$TMP/make_lambert_fixture" "$FIXDIR/lambert_sfc.grib2"

echo "compiling + generating wind (u/v) GRIB2 fixture..."
g++ "$ROOT/tools/make_wind_fixture.c" -I"$VI/include" -L"$VI/lib" \
    -leccodes -leccodes_memfs -lopenjp2 -lpng16 -lz -lm \
    -Wl,-rpath,"$VI/lib" -o "$TMP/make_wind_fixture"
"$TMP/make_wind_fixture" "$FIXDIR/wind_uv_850.grib2"

echo "compiling + generating GRIB2 missing-without-bitmap fixture..."
g++ "$ROOT/tools/make_grib_missing_fixture.c" -I"$VI/include" -L"$VI/lib" \
    -leccodes -leccodes_memfs -lopenjp2 -lpng16 -lz -lm \
    -Wl,-rpath,"$VI/lib" -o "$TMP/make_grib_missing_fixture"
"$TMP/make_grib_missing_fixture" "$FIXDIR/regular_ll_missing.grib2"

echo "compiling make_netcdf_fixture..."
# Static libnetcdf drags in HDF5 and curl (DAP) with its own chain. No szip or
# libaec: vcpkg.json builds HDF5 and ecCodes without them.
g++ "$ROOT/tools/make_netcdf_fixture.c" -I"$VI/include" -L"$VI/lib" \
    -lnetcdf -lhdf5_hl -lhdf5 \
    -lcurl -lssl -lcrypto -lbrotlidec -lbrotlicommon -lbz2 -llzma -lz -ltinyxml2 -lzstd \
    -lm -ldl -lpthread \
    -Wl,-rpath,"$VI/lib" -o "$TMP/make_netcdf_fixture"

echo "generating ERA5-shaped NetCDF fixture..."
"$TMP/make_netcdf_fixture" "$FIXDIR/era5_t_pl.nc"

echo "compiling + generating NetCDF height-level / T-time fixture..."
g++ "$ROOT/tools/make_netcdf_levels_fixture.c" -I"$VI/include" -L"$VI/lib" \
    -lnetcdf -lhdf5_hl -lhdf5 \
    -lcurl -lssl -lcrypto -lbrotlidec -lbrotlicommon -lbz2 -llzma -lz -ltinyxml2 -lzstd \
    -lm -ldl -lpthread \
    -Wl,-rpath,"$VI/lib" -o "$TMP/make_netcdf_levels_fixture"
"$TMP/make_netcdf_levels_fixture" "$FIXDIR/height_levels.nc"

echo "compiling + generating NetCDF Gaussian (non-uniform latitude) fixture..."
g++ "$ROOT/tools/make_netcdf_gaussian_fixture.c" -I"$VI/include" -L"$VI/lib" \
    -lnetcdf -lhdf5_hl -lhdf5 \
    -lcurl -lssl -lcrypto -lbrotlidec -lbrotlicommon -lbz2 -llzma -lz -ltinyxml2 -lzstd \
    -lm -ldl -lpthread \
    -Wl,-rpath,"$VI/lib" -o "$TMP/make_netcdf_gaussian_fixture"
"$TMP/make_netcdf_gaussian_fixture" "$FIXDIR/gaussian_lat.nc"

echo "generating synthetic ARL fixture..."
python3 "$ROOT/tools/make_arl_fixture.py" "$FIXDIR/small_latlon.arl"
# The fuzz corpus carries a copy (see tests/fuzz/corpus/README.md); keep it current.
cp "$FIXDIR/small_latlon.arl" "$ROOT/tests/fuzz/corpus/small_latlon.arl"

echo "done. fixtures in $FIXDIR:"
ls -l "$FIXDIR"
