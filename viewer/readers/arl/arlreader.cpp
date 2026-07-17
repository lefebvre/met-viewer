#include "viewer/readers/arl/arlreader.h"

#include <cctype>
#include <cstring>

#include "viewer/readers/arl/arldataset.h"

namespace met::readers::arl {

int ArlReader::probe(std::span<const std::byte> head, std::string_view /*path*/) const {
    if (head.size() < 18) return 0;
    const auto* b = reinterpret_cast<const unsigned char*>(head.data());
    // The first record's 50-byte label starts with six I2 time/level fields
    // (year, month, day, hour, forecast, level — ASCII digits, may contain
    // spaces) in bytes 0-11, then a 2-char alphanumeric grid ID (A2, e.g. "AA"
    // for HRRR) in bytes 12-13, then the 4-char variable name; the first record
    // of an ARL file is always "INDX".
    if (std::memcmp(b + 14, "INDX", 4) != 0) return 0;
    for (int i = 0; i < 12; ++i) {
        if (!std::isdigit(b[i]) && b[i] != ' ') return 0;
    }
    return 90;
}

std::unique_ptr<IDataset> ArlReader::open(const std::filesystem::path& path) const {
    return std::make_unique<ArlDataset>(path);
}

}  // namespace met::readers::arl
