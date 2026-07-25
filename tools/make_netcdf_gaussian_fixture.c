/* Generates a NetCDF-4 fixture with a NON-uniform (Gaussian) latitude axis.
 *
 * RegularLatLonGrid can only describe evenly-spaced axes, so the reader used to
 * take lat[1]-lat[0] as the spacing and extrapolate — silently mis-placing every
 * row of a Gaussian grid, worst toward the poles. This fixture exists so the
 * reader's uniformity check has something real to reject.
 *
 *   dims: latitude(8, N->S, Gaussian), longitude(16, 0..360 step 2)
 *   t : NC_FLOAT, CF metadata otherwise identical to era5_t_pl.nc
 *
 * The latitudes are the northern half of a real N4 Gaussian grid mirrored about
 * the equator, i.e. genuinely uneven rather than jittered.
 */
#include <netcdf.h>
#include <stdio.h>

#define NLAT 8
#define NLON 16

#define CHECK(e)                                                    \
    do {                                                            \
        int _s = (e);                                               \
        if (_s != NC_NOERR) {                                       \
            fprintf(stderr, "netcdf error: %s\n", nc_strerror(_s)); \
            return 1;                                               \
        }                                                           \
    } while (0)

int main(int argc, char** argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <out.nc>\n", argv[0]);
        return 2;
    }

    /* Gaussian latitudes for N=4 (8 rows), north to south. Steps run
     * 19.87, 16.79, 16.34, 16.34, 16.34, 16.79, 19.87 degrees — a spread no
     * uniform-spacing model can represent. */
    static const double lat[NLAT] = {74.75, 54.88, 38.09, 21.75,
                                     -21.75, -38.09, -54.88, -74.75};
    double lon[NLON];
    for (int i = 0; i < NLON; ++i) lon[i] = 2.0 * i;

    float t[NLAT][NLON];
    for (int j = 0; j < NLAT; ++j)
        for (int i = 0; i < NLON; ++i)
            t[j][i] = (float)(273.15 + 0.1 * lon[i] - 0.2 * lat[j]);

    int ncid = 0, latDim = 0, lonDim = 0, latVar = 0, lonVar = 0, tVar = 0;
    CHECK(nc_create(argv[1], NC_NETCDF4 | NC_CLOBBER, &ncid));
    CHECK(nc_def_dim(ncid, "latitude", NLAT, &latDim));
    CHECK(nc_def_dim(ncid, "longitude", NLON, &lonDim));

    CHECK(nc_def_var(ncid, "latitude", NC_DOUBLE, 1, &latDim, &latVar));
    CHECK(nc_put_att_text(ncid, latVar, "units", 13, "degrees_north"));
    CHECK(nc_put_att_text(ncid, latVar, "standard_name", 8, "latitude"));

    CHECK(nc_def_var(ncid, "longitude", NC_DOUBLE, 1, &lonDim, &lonVar));
    CHECK(nc_put_att_text(ncid, lonVar, "units", 12, "degrees_east"));
    CHECK(nc_put_att_text(ncid, lonVar, "standard_name", 9, "longitude"));

    const int dims[2] = {latDim, lonDim};
    CHECK(nc_def_var(ncid, "t", NC_FLOAT, 2, dims, &tVar));
    CHECK(nc_put_att_text(ncid, tVar, "units", 1, "K"));
    CHECK(nc_put_att_text(ncid, tVar, "long_name", 11, "Temperature"));
    CHECK(nc_put_att_text(ncid, tVar, "standard_name", 15, "air_temperature"));
    CHECK(nc_enddef(ncid));

    CHECK(nc_put_var_double(ncid, latVar, lat));
    CHECK(nc_put_var_double(ncid, lonVar, lon));
    CHECK(nc_put_var_float(ncid, tVar, &t[0][0]));
    CHECK(nc_close(ncid));

    printf("wrote %s\n", argv[1]);
    return 0;
}
