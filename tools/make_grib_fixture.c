/* Generates a tiny GRIB2 fixture with a known analytic field for reader tests.
 *
 * The field is  value(lat,lon) = 273.15 + 0.1*lon - 0.2*lat  (Kelvin), which
 * varies distinctly in both directions so a transposed / mis-scanned read is
 * detectable. Grid: regular_ll, 16 lon x 8 lat, first point (lat=70, lon=0),
 * 2-degree spacing scanning north->south, west->east. Parameter: temperature
 * (discipline 0, category 0, number 0) on the 500 hPa isobaric level.
 *
 * Build (from repo root, release preset installed):
 *   VI=build/release/vcpkg_installed/x64-linux
 *   cc tools/make_grib_fixture.c -I$VI/include -L$VI/lib -leccodes -lm \
 *      -Wl,-rpath,$VI/lib -o /tmp/make_grib_fixture
 * Run:
 *   ECCODES_DEFINITION_PATH=$VI/share/eccodes/definitions \
 *   ECCODES_SAMPLES_PATH=$VI/share/eccodes/samples \
 *   /tmp/make_grib_fixture tests/fixtures/regular_ll_t500.grib2
 */
#include <eccodes.h>
#include <stdio.h>
#include <stdlib.h>

#define NI 16
#define NJ 8
#define LAT0 70.0
#define LON0 0.0
#define DLON 2.0
#define DLAT 2.0

int main(int argc, char** argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <out.grib2>\n", argv[0]);
        return 2;
    }

    codes_handle* h = codes_grib_handle_new_from_samples(NULL, "GRIB2");
    if (!h) {
        fprintf(stderr, "failed to load GRIB2 sample\n");
        return 1;
    }

    int err = 0;
#define SET_L(k, v) do { if ((err = codes_set_long(h, k, v))) goto fail; } while (0)
#define SET_D(k, v) do { if ((err = codes_set_double(h, k, v))) goto fail; } while (0)

    SET_L("Ni", NI);
    SET_L("Nj", NJ);
    SET_D("latitudeOfFirstGridPointInDegrees", LAT0);
    SET_D("longitudeOfFirstGridPointInDegrees", LON0);
    SET_D("latitudeOfLastGridPointInDegrees", LAT0 - DLAT * (NJ - 1));
    SET_D("longitudeOfLastGridPointInDegrees", LON0 + DLON * (NI - 1));
    SET_D("iDirectionIncrementInDegrees", DLON);
    SET_D("jDirectionIncrementInDegrees", DLAT);
    /* scanning mode: -i +... default is +i, -j (north to south). Keep default. */

    /* Temperature at 500 hPa isobaric surface. */
    SET_L("discipline", 0);
    SET_L("parameterCategory", 0);
    SET_L("parameterNumber", 0);
    SET_L("typeOfFirstFixedSurface", 100);         /* isobaric surface */
    SET_L("scaleFactorOfFirstFixedSurface", 0);
    SET_L("scaledValueOfFirstFixedSurface", 50000); /* 50000 Pa = 500 hPa */

    /* Fill values in GRIB scan order: j outer (north->south), i inner (west->east). */
    double values[NI * NJ];
    for (int j = 0; j < NJ; ++j) {
        double lat = LAT0 - DLAT * j;
        for (int i = 0; i < NI; ++i) {
            double lon = LON0 + DLON * i;
            values[j * NI + i] = 273.15 + 0.1 * lon - 0.2 * lat;
        }
    }
    if ((err = codes_set_double_array(h, "values", values, NI * NJ))) goto fail;

    if ((err = codes_write_message(h, argv[1], "w"))) goto fail;

    codes_handle_delete(h);
    printf("wrote %s (%dx%d regular_ll t@500hPa)\n", argv[1], NI, NJ);
    return 0;

fail:
    fprintf(stderr, "eccodes error: %s\n", codes_get_error_message(err));
    codes_handle_delete(h);
    return 1;
}
