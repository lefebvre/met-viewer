#include "viewer/readers/grib/gribreader.h"

#include <cstring>

#include "viewer/readers/grib/gribdataset.h"

namespace met::readers::grib {

int GribReader::probe(std::span<const std::byte> head, std::string_view /*path*/) const {
    // Scan the header window for the "GRIB" magic; real files sometimes carry a
    // WMO/TDCF preamble before the message start.
    static constexpr char kMagic[4] = {'G', 'R', 'I', 'B'};
    if (head.size() < 4) return 0;
    for (std::size_t i = 0; i + 4 <= head.size(); ++i) {
        if (std::memcmp(head.data() + i, kMagic, 4) == 0) return 100;
    }
    return 0;
}

std::unique_ptr<IDataset> GribReader::open(const std::filesystem::path& path) const {
    return std::make_unique<GribDataset>(path);
}

}  // namespace met::readers::grib
