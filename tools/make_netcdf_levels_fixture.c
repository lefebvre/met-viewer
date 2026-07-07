/* Generates a NetCDF-4 fixture with a NON-pressure vertical axis (height in
 * metres) and an ISO-8601 'T'-separated time reference. Exercises the CF
 * reader's vertical-coordinate classification (height/sigma/hybrid/model — not
 * just pressure, so multi-level variables are not collapsed to a single slice)
 * and its time-unit parsing (the 'T' date/time separator must not drop the
 * time-of-day).
 *
 *   dims: time(2), height(4), latitude(4, N->S), longitude(4, 0..)
 *   height[10,50,100,500] m (standard_name "height", positive "up")
 *   time = hours since 2020-06-01T06:00:00.0  ->  values {0, 6}
 *   t(time,height,lat,lon) = 300 - 5*heightIndex + timeIndex   (Kelvin)
 *     (distinct per level and time so a collapsed/wrong slice is detectable)
 */
#include <netcdf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NT 2
#define NH 4
#define NLAT 4
#define NLON 4

#define CHECK(e)                                                    \
    do {                                                            \
        int _s = (e);                                               \
        if (_s != NC_NOERR) {                                       \
            fprintf(stderr, "netcdf error: %s\n", nc_strerror(_s)); \
            return 1;                                               \
        }                                                           \
    } while (0)

static int put_text(int ncid, int var, const char* name, const char* val) {
    return nc_put_att_text(ncid, var, name, strlen(val), val);
}

int main(int argc, char** argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <out.nc>\n", argv[0]);
        return 2;
    }

    int ncid;
    CHECK(nc_create(argv[1], NC_NETCDF4 | NC_CLOBBER, &ncid));

    int dt, dh, dlat, dlon;
    CHECK(nc_def_dim(ncid, "time", NT, &dt));
    CHECK(nc_def_dim(ncid, "height", NH, &dh));
    CHECK(nc_def_dim(ncid, "latitude", NLAT, &dlat));
    CHECK(nc_def_dim(ncid, "longitude", NLON, &dlon));

    int vtime, vh, vlat, vlon, vt;
    CHECK(nc_def_var(ncid, "time", NC_DOUBLE, 1, &dt, &vtime));
    CHECK(nc_def_var(ncid, "height", NC_DOUBLE, 1, &dh, &vh));
    CHECK(nc_def_var(ncid, "latitude", NC_DOUBLE, 1, &dlat, &vlat));
    CHECK(nc_def_var(ncid, "longitude", NC_DOUBLE, 1, &dlon, &vlon));
    const int tdims[4] = {dt, dh, dlat, dlon};
    CHECK(nc_def_var(ncid, "t", NC_FLOAT, 4, tdims, &vt));

    CHECK(put_text(ncid, vtime, "units", "hours since 2020-06-01T06:00:00.0"));
    CHECK(put_text(ncid, vtime, "standard_name", "time"));

    CHECK(put_text(ncid, vh, "units", "m"));
    CHECK(put_text(ncid, vh, "standard_name", "height"));
    CHECK(put_text(ncid, vh, "positive", "up"));

    CHECK(put_text(ncid, vlat, "units", "degrees_north"));
    CHECK(put_text(ncid, vlat, "standard_name", "latitude"));
    CHECK(put_text(ncid, vlon, "units", "degrees_east"));
    CHECK(put_text(ncid, vlon, "standard_name", "longitude"));

    CHECK(put_text(ncid, vt, "units", "K"));
    CHECK(put_text(ncid, vt, "long_name", "Temperature"));
    CHECK(put_text(ncid, vt, "standard_name", "air_temperature"));

    CHECK(nc_enddef(ncid));

    double lat[NLAT], lon[NLON];
    double hgt[NH] = {10.0, 50.0, 100.0, 500.0};
    double tm[NT] = {0.0, 6.0};
    for (int j = 0; j < NLAT; ++j) lat[j] = 60.0 - 2.0 * j;
    for (int i = 0; i < NLON; ++i) lon[i] = 0.0 + 2.0 * i;
    CHECK(nc_put_var_double(ncid, vlat, lat));
    CHECK(nc_put_var_double(ncid, vlon, lon));
    CHECK(nc_put_var_double(ncid, vh, hgt));
    CHECK(nc_put_var_double(ncid, vtime, tm));

    const size_t n = (size_t)NT * NH * NLAT * NLON;
    float* td = (float*)malloc(sizeof(float) * n);
    for (int t = 0; t < NT; ++t)
        for (int l = 0; l < NH; ++l)
            for (int j = 0; j < NLAT; ++j)
                for (int i = 0; i < NLON; ++i) {
                    const size_t idx = ((size_t)t * NH + l) * NLAT * NLON + (size_t)j * NLON + i;
                    td[idx] = (float)(300.0 - 5.0 * l + (double)t);
                }
    CHECK(nc_put_var_float(ncid, vt, td));
    free(td);

    CHECK(nc_close(ncid));
    printf("wrote %s (t: %dx%dx%dx%d, height axis + T-separator time)\n", argv[1], NT, NH, NLAT,
           NLON);
    return 0;
}
