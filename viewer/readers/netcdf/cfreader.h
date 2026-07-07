#pragma once

#include "viewer/readers/ireader.h"

namespace met::readers::netcdf {

// Format handler for NetCDF (classic and NetCDF-4/HDF5) files with CF metadata.
class CfReader : public IFormatReader {
public:
    [[nodiscard]] int probe(std::span<const std::byte> head,
                            std::string_view path) const override;
    [[nodiscard]] std::unique_ptr<IDataset> open(
        const std::filesystem::path& path) const override;
    [[nodiscard]] std::string name() const override { return "NetCDF/CF"; }
};

}  // namespace met::readers::netcdf
