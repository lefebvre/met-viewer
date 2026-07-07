#include "viewer/readers/netcdf/cfreader.h"

#include <cstring>

#include "viewer/readers/netcdf/cfdataset.h"

namespace met::readers::netcdf {

int CfReader::probe(std::span<const std::byte> head, std::string_view /*path*/) const {
    if (head.size() < 8) return 0;
    const auto* b = reinterpret_cast<const unsigned char*>(head.data());
    // HDF5 (NetCDF-4) superblock signature.
    static const unsigned char kHdf5[8] = {0x89, 'H', 'D', 'F', '\r', '\n', 0x1a, '\n'};
    if (std::memcmp(b, kHdf5, 8) == 0) return 90;
    // NetCDF classic (CDF\x01) and 64-bit offset (CDF\x02) and CDF5 (CDF\x05).
    if (b[0] == 'C' && b[1] == 'D' && b[2] == 'F' && (b[3] == 1 || b[3] == 2 || b[3] == 5))
        return 95;
    return 0;
}

std::unique_ptr<IDataset> CfReader::open(const std::filesystem::path& path) const {
    return std::make_unique<CfDataset>(path);
}

}  // namespace met::readers::netcdf
