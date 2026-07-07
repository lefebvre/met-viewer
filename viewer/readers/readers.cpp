#include "viewer/readers/readers.h"

#include <eccodes.h>
#include <netcdf.h>

namespace met::readers {

int placeholder() {
    // Smoke-test that ecCodes and netcdf-c actually link through the vcpkg toolchain.
    const long eccodesVersion = codes_get_api_version();
    const char* netcdfVersion = nc_inq_libvers();

    return (eccodesVersion > 0 && netcdfVersion != nullptr) ? 0 : 1;
}

}  // namespace met::readers
