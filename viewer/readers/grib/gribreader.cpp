#include "viewer/readers/grib/gribreader.h"

#include <cstddef>
#include <cstring>

#include "viewer/readers/grib/gribdataset.h"

namespace met::readers::grib {

int GribReader::probe(std::span<const std::byte> head, std::string_view /*path*/) const {
    // Scan the header window for the "GRIB" magic; real files sometimes carry a
    // WMO/TDCF preamble before the message start. Validate the edition number
    // (byte 7 of the message) so a file that merely contains the ASCII bytes
    // "GRIB" somewhere is not misdetected as GRIB.
    static constexpr char kMagic[4] = {'G', 'R', 'I', 'B'};
    if (head.size() < 8) return 0;
    for (std::size_t i = 0; i + 8 <= head.size(); ++i) {
        if (std::memcmp(head.data() + i, kMagic, 4) == 0) {
            const unsigned edition = std::to_integer<unsigned>(head[i + 7]);
            if (edition == 1 || edition == 2) return 100;
        }
    }
    return 0;
}

std::unique_ptr<IDataset> GribReader::open(const std::filesystem::path& path) const {
    return std::make_unique<GribDataset>(path);
}

}  // namespace met::readers::grib
