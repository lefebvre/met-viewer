/* Generates an ERA5-shaped NetCDF-4 fixture for the CF reader and analysis
 * tools (cross-section / sounding / time-series).
 *
 *   dims: time(2), pressure_level(9), latitude(8, N->S), longitude(16, 0..360)
 *   coords: latitude[70..56 step -2], longitude[0..30 step 2],
 *           pressure_level[1000,925,850,700,500,300,250,200,100] hPa,
 *           time (hours since 1900-01-01)
 *   t : NC_SHORT packed (scale_factor/add_offset, _FillValue) as ERA5 ships.
 *   r : NC_FLOAT relative humidity (%), unpacked.
 *
 *   base(lat,lon) = 273.15 + 0.1*lon - 0.2*lat
 *   t = base + 0.06*(plev - 500) + timeIndex   [t@500,time0 == the GRIB fixture]
 *   r = 40 + 40*(plev/1000)                     [higher RH lower down]
 * One t cell at (850 hPa, time 1, j=0, i=0) is set to _FillValue.
 */
#include <math.h>
#include <netcdf.h>
#include <stdio.h>
#include <stdlib.h>

#define NT 2
#define NL 9
#define NLAT 8
#define NLON 16

#define CHECK(e)                                                   \
    do {                                                           \
        int _s = (e);                                              \
        if (_s != NC_NOERR) {                                      \
            fprintf(stderr, "netcdf error: %s\n", nc_strerror(_s)); \
            return 1;                                              \
        }                                                          \
    } while (0)

int main(int argc, char** argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <out.nc>\n", argv[0]);
        return 2;
    }

    int ncid;
    CHECK(nc_create(argv[1], NC_NETCDF4 | NC_CLOBBER, &ncid));

    int dt, dl, dlat, dlon;
    CHECK(nc_def_dim(ncid, "time", NT, &dt));
    CHECK(nc_def_dim(ncid, "pressure_level", NL, &dl));
    CHECK(nc_def_dim(ncid, "latitude", NLAT, &dlat));
    CHECK(nc_def_dim(ncid, "longitude", NLON, &dlon));

    int vtime, vlev, vlat, vlon, vt, vr;
    CHECK(nc_def_var(ncid, "time", NC_DOUBLE, 1, &dt, &vtime));
    CHECK(nc_def_var(ncid, "pressure_level", NC_DOUBLE, 1, &dl, &vlev));
    CHECK(nc_def_var(ncid, "latitude", NC_DOUBLE, 1, &dlat, &vlat));
    CHECK(nc_def_var(ncid, "longitude", NC_DOUBLE, 1, &dlon, &vlon));
    const int tdims[4] = {dt, dl, dlat, dlon};
    CHECK(nc_def_var(ncid, "t", NC_SHORT, 4, tdims, &vt));
    CHECK(nc_def_var(ncid, "r", NC_FLOAT, 4, tdims, &vr));

    CHECK(nc_put_att_text(ncid, vtime, "units", 33, "hours since 1900-01-01 00:00:00.0"));
    CHECK(nc_put_att_text(ncid, vtime, "calendar", 9, "gregorian"));
    CHECK(nc_put_att_text(ncid, vtime, "standard_name", 4, "time"));

    CHECK(nc_put_att_text(ncid, vlev, "units", 3, "hPa"));
    CHECK(nc_put_att_text(ncid, vlev, "standard_name", 21, "air_pressure_at_level"));
    CHECK(nc_put_att_text(ncid, vlev, "positive", 4, "down"));

    CHECK(nc_put_att_text(ncid, vlat, "units", 13, "degrees_north"));
    CHECK(nc_put_att_text(ncid, vlat, "standard_name", 8, "latitude"));
    CHECK(nc_put_att_text(ncid, vlon, "units", 12, "degrees_east"));
    CHECK(nc_put_att_text(ncid, vlon, "standard_name", 9, "longitude"));

    const double scale = 0.001, offset = 260.0;
    const short fill = -32767;
    CHECK(nc_put_att_double(ncid, vt, "scale_factor", NC_DOUBLE, 1, &scale));
    CHECK(nc_put_att_double(ncid, vt, "add_offset", NC_DOUBLE, 1, &offset));
    CHECK(nc_put_att_short(ncid, vt, "_FillValue", NC_SHORT, 1, &fill));
    CHECK(nc_put_att_text(ncid, vt, "units", 1, "K"));
    CHECK(nc_put_att_text(ncid, vt, "long_name", 11, "Temperature"));
    CHECK(nc_put_att_text(ncid, vt, "standard_name", 15, "air_temperature"));

    CHECK(nc_put_att_text(ncid, vr, "units", 1, "%"));
    CHECK(nc_put_att_text(ncid, vr, "long_name", 17, "Relative humidity"));
    CHECK(nc_put_att_text(ncid, vr, "standard_name", 17, "relative_humidity"));

    CHECK(nc_enddef(ncid));

    double lat[NLAT], lon[NLON];
    double lev[NL] = {1000.0, 925.0, 850.0, 700.0, 500.0, 300.0, 250.0, 200.0, 100.0};
    double time[NT] = {938268.0, 938269.0};
    for (int j = 0; j < NLAT; ++j) lat[j] = 70.0 - 2.0 * j;
    for (int i = 0; i < NLON; ++i) lon[i] = 0.0 + 2.0 * i;
    CHECK(nc_put_var_double(ncid, vlat, lat));
    CHECK(nc_put_var_double(ncid, vlon, lon));
    CHECK(nc_put_var_double(ncid, vlev, lev));
    CHECK(nc_put_var_double(ncid, vtime, time));

    const size_t n = (size_t)NT * NL * NLAT * NLON;
    short* tdata = (short*)malloc(sizeof(short) * n);
    float* rdata = (float*)malloc(sizeof(float) * n);
    for (int t = 0; t < NT; ++t) {
        for (int l = 0; l < NL; ++l) {
            for (int j = 0; j < NLAT; ++j) {
                for (int i = 0; i < NLON; ++i) {
                    const size_t idx = ((size_t)t * NL + l) * NLAT * NLON + (size_t)j * NLON + i;
                    const double base = 273.15 + 0.1 * lon[i] - 0.2 * lat[j];
                    const double tv = base + 0.06 * (lev[l] - 500.0) + (double)t;
                    short packed = (short)lround((tv - offset) / scale);
                    if (t == 1 && l == 2 && j == 0 && i == 0) packed = fill;
                    tdata[idx] = packed;
                    rdata[idx] = (float)(40.0 + 40.0 * (lev[l] / 1000.0));
                }
            }
        }
    }
    CHECK(nc_put_var_short(ncid, vt, tdata));
    CHECK(nc_put_var_float(ncid, vr, rdata));
    free(tdata);
    free(rdata);

    CHECK(nc_close(ncid));
    printf("wrote %s (t,r: %dx%dx%dx%d)\n", argv[1], NT, NL, NLAT, NLON);
    return 0;
}
