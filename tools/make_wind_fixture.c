/* Generates a GRIB2 fixture with U and V wind components on a regular lat/lon
 * grid, for wind pairing / barb tests. Two messages (u, v) at 850 hPa.
 *
 * Analytic wind: a solid-body-like rotation so barbs form a recognizable
 * pattern. u(lat,lon) = -A*sin(lon*pi/180), v(lat,lon) = A*cos(lon*pi/180),
 * with A = 25 m/s. Grid: 16 lon x 8 lat, first point (60N, 0), 2-degree spacing.
 *
 * Build/run: see tools/make_fixtures.sh.
 */
#include <eccodes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define NI 16
#define NJ 8
#define LAT0 60.0
#define LON0 0.0
#define DLON 2.0
#define DLAT 2.0
#define AMP 25.0

static int write_component(const char* path, const char* mode, const char* shortName,
                           int paramNumber, double sign_is_u) {
    codes_handle* h = codes_grib_handle_new_from_samples(NULL, "GRIB2");
    if (!h) return 1;
    int err = 0;
    size_t slen;
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
    slen = 13;
    if ((err = codes_set_string(h, "typeOfLevel", "isobaricInhPa", &slen))) goto fail;
    SET_L("level", 850);
    /* discipline 0 (meteorological), category 2 (momentum), number 2=u, 3=v */
    SET_L("discipline", 0);
    SET_L("parameterCategory", 2);
    SET_L("parameterNumber", paramNumber);
    slen = 4;
    codes_set_string(h, "shortName", shortName, &slen);

    double values[NI * NJ];
    for (int j = 0; j < NJ; ++j) {
        for (int i = 0; i < NI; ++i) {
            double lon = LON0 + DLON * i;
            double u = -AMP * sin(lon * M_PI / 180.0);
            double v = AMP * cos(lon * M_PI / 180.0);
            values[j * NI + i] = sign_is_u > 0.5 ? u : v;
        }
    }
    if ((err = codes_set_double_array(h, "values", values, NI * NJ))) goto fail;
    if ((err = codes_write_message(h, path, mode))) goto fail;
    codes_handle_delete(h);
    return 0;
fail:
    fprintf(stderr, "eccodes error: %s\n", codes_get_error_message(err));
    codes_handle_delete(h);
    return 1;
}

int main(int argc, char** argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <out.grib2>\n", argv[0]);
        return 2;
    }
    if (write_component(argv[1], "w", "u", 2, 1.0)) return 1;   /* U: category 2 number 2 */
    if (write_component(argv[1], "a", "v", 3, 0.0)) return 1;   /* V: category 2 number 3 */
    printf("wrote %s (u,v @ 850 hPa, %dx%d)\n", argv[1], NI, NJ);
    return 0;
}
