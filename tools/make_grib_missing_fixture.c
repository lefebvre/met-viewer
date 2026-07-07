/* Generates a tiny GRIB2 fixture whose field carries MISSING values encoded via
 * complex-packing missing-value management (data representation template 5.3) —
 * i.e. NO bitmap. This exercises the reader path that must honor declared
 * missing values even when bitmapPresent==0, so the missingValue sentinel does
 * not leak through as real data.
 *
 * Grid: regular_ll 4x4, first point (70,0), 2-degree spacing. Values are
 * 280 + index, with three cells (5, 6, 10) set missing (9999).
 *
 * After writing, the fixture is reopened and its bitmapPresent / numberOfMissing
 * are printed so the encoding can be confirmed.
 */
#include <eccodes.h>
#include <stdio.h>
#include <stdlib.h>

#define NI 4
#define NJ 4

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
    SET_D("latitudeOfFirstGridPointInDegrees", 70.0);
    SET_D("longitudeOfFirstGridPointInDegrees", 0.0);
    SET_D("latitudeOfLastGridPointInDegrees", 70.0 - 2.0 * (NJ - 1));
    SET_D("longitudeOfLastGridPointInDegrees", 0.0 + 2.0 * (NI - 1));
    SET_D("iDirectionIncrementInDegrees", 2.0);
    SET_D("jDirectionIncrementInDegrees", 2.0);
    {
        size_t l = 13;
        if ((err = codes_set_string(h, "typeOfLevel", "isobaricInhPa", &l))) goto fail;
    }
    SET_L("level", 500);

    /* Complex packing supports missing-value management without a bitmap. Set it
     * before writing the values so the missing sentinels are encoded via the
     * template rather than requiring a bitmap. */
    {
        size_t l = 12;
        if ((err = codes_set_string(h, "packingType", "grid_complex", &l))) goto fail;
    }
    SET_L("bitmapPresent", 0);
    SET_D("missingValue", 9999.0);

    double vals[NI * NJ];
    for (int k = 0; k < NI * NJ; ++k) vals[k] = 280.0 + (double)k;
    vals[5] = 9999.0;
    vals[6] = 9999.0;
    vals[10] = 9999.0;
    if ((err = codes_set_double_array(h, "values", vals, NI * NJ))) goto fail;

    if ((err = codes_write_message(h, argv[1], "w"))) goto fail;
    codes_handle_delete(h);

    /* Reopen and report the encoding so we can confirm bitmap-free missing. */
    {
        FILE* f = fopen(argv[1], "rb");
        int e2 = 0;
        codes_handle* r = codes_grib_handle_new_from_file(NULL, f, &e2);
        long nm = -1, bp = -1;
        double mv = 0;
        codes_get_long(r, "numberOfMissing", &nm);
        codes_get_long(r, "bitmapPresent", &bp);
        codes_get_double(r, "missingValue", &mv);
        size_t vn = NI * NJ;
        double out[NI * NJ];
        codes_get_double_array(r, "values", out, &vn);
        printf("wrote %s (%dx%d)\n", argv[1], NI, NJ);
        printf("readback: bitmapPresent=%ld numberOfMissing=%ld missingValue=%g\n", bp, nm, mv);
        printf("val[0]=%g val[5]=%g val[6]=%g val[10]=%g\n", out[0], out[5], out[6], out[10]);
        codes_handle_delete(r);
        fclose(f);
    }
    return 0;

fail:
    fprintf(stderr, "eccodes error: %s\n", codes_get_error_message(err));
    codes_handle_delete(h);
    return 1;
}
