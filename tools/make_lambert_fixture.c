/* Generates a small Lambert-conformal GRIB2 fixture for projected-grid tests.
 *
 * Grid keys describe a tangent LCC (Latin1==Latin2). Values are linear in grid
 * index, value(i,j) = 250 + 0.5*i + 0.3*j, so a correct decode reproduces them
 * exactly at every (col,row) regardless of the projection. The reader is checked
 * separately for projection correctness (indexToLatLon(0,0) must equal the first
 * grid point stated in the keys).
 *
 * Build/run: see tools/make_fixtures.sh.
 */
#include <eccodes.h>
#include <stdio.h>
#include <stdlib.h>

#define NX 16
#define NY 12
#define LAT1 30.0    /* first grid point latitude  */
#define LON1 250.0   /* first grid point longitude */
#define LATIN 40.0   /* standard parallel (tangent) */
#define LOV 260.0    /* orientation / central meridian */
#define DX 50000.0   /* metres */
#define DY 50000.0

int main(int argc, char** argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <out.grib2>\n", argv[0]);
        return 2;
    }

    /* Start from the polar-stereographic sample (a GRIB2 projected template) and
     * switch it to Lambert; then set every Lambert key. */
    codes_handle* h = codes_grib_handle_new_from_samples(NULL, "polar_stereographic_sfc_grib2");
    if (!h) {
        fprintf(stderr, "failed to load projected sample\n");
        return 1;
    }

    int err = 0;
    size_t slen = 7;
#define SET_L(k, v) do { if ((err = codes_set_long(h, k, v))) goto fail; } while (0)
#define SET_D(k, v) do { if ((err = codes_set_double(h, k, v))) goto fail; } while (0)

    if ((err = codes_set_string(h, "gridType", "lambert", &slen))) goto fail;
    SET_L("Nx", NX);
    SET_L("Ny", NY);
    SET_D("latitudeOfFirstGridPointInDegrees", LAT1);
    SET_D("longitudeOfFirstGridPointInDegrees", LON1);
    SET_D("Latin1InDegrees", LATIN);
    SET_D("Latin2InDegrees", LATIN);
    SET_D("LaDInDegrees", LATIN);
    SET_D("LoVInDegrees", LOV);
    SET_D("DxInMetres", DX);
    SET_D("DyInMetres", DY);
    SET_L("iScansNegatively", 0);
    SET_L("jScansPositively", 1);   /* scan north (projected +y) */

    slen = 13;
    if ((err = codes_set_string(h, "typeOfLevel", "surface", &slen))) goto fail;

    double values[NX * NY];
    for (int j = 0; j < NY; ++j)
        for (int i = 0; i < NX; ++i)
            values[j * NX + i] = 250.0 + 0.5 * i + 0.3 * j;
    if ((err = codes_set_double_array(h, "values", values, NX * NY))) goto fail;

    if ((err = codes_write_message(h, argv[1], "w"))) goto fail;
    codes_handle_delete(h);
    printf("wrote %s (%dx%d lambert)\n", argv[1], NX, NY);
    return 0;

fail:
    fprintf(stderr, "eccodes error: %s\n", codes_get_error_message(err));
    codes_handle_delete(h);
    return 1;
}
