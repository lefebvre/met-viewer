#pragma once

#include "viewer/readers/ireader.h"

namespace met::readers::arl {

// Format handler for NOAA ARL packed meteorology. No magic number; detected by
// the first record being an INDX label (ASCII date digits + "INDX").
class ArlReader : public IFormatReader {
public:
    [[nodiscard]] int probe(std::span<const std::byte> head,
                            std::string_view path) const override;
    [[nodiscard]] std::unique_ptr<IDataset> open(
        const std::filesystem::path& path) const override;
    [[nodiscard]] std::string name() const override { return "ARL"; }
};

}  // namespace met::readers::arl
