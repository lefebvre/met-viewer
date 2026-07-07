#pragma once

#include "viewer/readers/ireader.h"

namespace met::readers::grib {

// Format handler for GRIB edition 1 and 2 (ecCodes). Detects the "GRIB" magic
// anywhere in the header window (WMO/TDCF preambles occur in real files).
class GribReader : public IFormatReader {
public:
    [[nodiscard]] int probe(std::span<const std::byte> head,
                            std::string_view path) const override;
    [[nodiscard]] std::unique_ptr<IDataset> open(
        const std::filesystem::path& path) const override;
    [[nodiscard]] std::string name() const override { return "GRIB"; }
};

}  // namespace met::readers::grib
