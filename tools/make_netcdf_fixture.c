/* Generates a tiny ERA5-shaped NetCDF-4 fixture for CF reader tests.
 *
 * Layout mirrors an ERA5 pressure-level download:
 *   dims: time(2), pressure_level(3), latitude(8, N->S), longitude(16, 0..360)
 *   coords: latitude[70..56 step -2], longitude[0..30 step 2],
 *           pressure_level[250,500,850] (hPa), time (hours since 1900-01-01)
 *   data: t(time, pressure_level, latitude, longitude) packed as NC_SHORT with
 *         scale_factor/add_offset and a _FillValue, exactly as ERA5 ships.
 *
 * The unpacked value is:
 *   base(lat,lon) = 273.15 + 0.1*lon - 0.2*lat            [matches the GRIB fixture]
 *   value = base + (plev-500)*0.001 + timeIndex*1.0
 * so at (500 hPa, time index 0) the field equals the GRIB fixture, while other
 * levels/times differ detectably. One cell at (850 hPa, time 1) is set to fill.
 *
 * Build/run: see tools/make_fixtures.sh.
 */
#include <math.h>
#include <netcdf.h>
#include <stdio.h>
#include <stdlib.h>

#define NT 2
#define NL 3
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

    int vtime, vlev, vlat, vlon, vt;
    CHECK(nc_def_var(ncid, "time", NC_DOUBLE, 1, &dt, &vtime));
    CHECK(nc_def_var(ncid, "pressure_level", NC_DOUBLE, 1, &dl, &vlev));
    CHECK(nc_def_var(ncid, "latitude", NC_DOUBLE, 1, &dlat, &vlat));
    CHECK(nc_def_var(ncid, "longitude", NC_DOUBLE, 1, &dlon, &vlon));
    const int tdims[4] = {dt, dl, dlat, dlon};
    CHECK(nc_def_var(ncid, "t", NC_SHORT, 4, tdims, &vt));

    // CF attributes.
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

    CHECK(nc_enddef(ncid));

    // Coordinate values.
    double lat[NLAT], lon[NLON], lev[NL] = {250.0, 500.0, 850.0};
    double time[NT] = {938268.0, 938269.0};  // arbitrary hours since 1900
    for (int j = 0; j < NLAT; ++j) lat[j] = 70.0 - 2.0 * j;   // N->S
    for (int i = 0; i < NLON; ++i) lon[i] = 0.0 + 2.0 * i;    // 0..30, 0-360 convention
    CHECK(nc_put_var_double(ncid, vlat, lat));
    CHECK(nc_put_var_double(ncid, vlon, lon));
    CHECK(nc_put_var_double(ncid, vlev, lev));
    CHECK(nc_put_var_double(ncid, vtime, time));

    // Packed data.
    short* data = (short*)malloc(sizeof(short) * NT * NL * NLAT * NLON);
    for (int t = 0; t < NT; ++t) {
        for (int l = 0; l < NL; ++l) {
            for (int j = 0; j < NLAT; ++j) {
                for (int i = 0; i < NLON; ++i) {
                    const size_t idx =
                        ((size_t)t * NL + l) * NLAT * NLON + (size_t)j * NLON + i;
                    const double base = 273.15 + 0.1 * lon[i] - 0.2 * lat[j];
                    const double value = base + (lev[l] - 500.0) * 0.001 + (double)t * 1.0;
                    short packed = (short)lround((value - offset) / scale);
                    // Poke one fill cell at (850 hPa, time 1, j=0, i=0).
                    if (t == 1 && l == 2 && j == 0 && i == 0) packed = fill;
                    data[idx] = packed;
                }
            }
        }
    }
    CHECK(nc_put_var_short(ncid, vt, data));
    free(data);

    CHECK(nc_close(ncid));
    printf("wrote %s (t: %dx%dx%dx%d packed short)\n", argv[1], NT, NL, NLAT, NLON);
    return 0;
}
