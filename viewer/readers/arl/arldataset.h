#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "viewer/core/grid.h"
#include "viewer/readers/ireader.h"

namespace met::readers::arl {

// A NOAA ARL packed dataset (HYSPLIT meteorology). The file is a sequence of
// fixed-length records (NX*NY + 50). The first record of each time period is an
// "INDX" record whose ASCII data section defines the grid and the per-level
// variable inventory. Data records carry a 50-byte label (time, level, variable,
// packing exponent, and VAR1 = the exact value of grid point (1,1)) followed by
// one byte per grid point, packed as scaled running differences.
//
// Thread-safety: readField() re-opens the file per call, so concurrent reads are
// safe. Catalog is built once at open time.
class ArlDataset : public IDataset {
public:
    explicit ArlDataset(std::filesystem::path path);

    [[nodiscard]] const core::DatasetCatalog& catalog() const override { return catalog_; }
    [[nodiscard]] core::Field2D readField(const core::FieldKey& key) override;
    [[nodiscard]] std::string formatName() const override { return "ARL"; }

private:
    void scan();

    std::filesystem::path path_;
    core::DatasetCatalog catalog_;
    core::GridDef grid_{core::RegularLatLonGrid{}};
    int nx_ = 0;
    int ny_ = 0;
    long recLen_ = 0;
    std::mutex mutex_;
};

}  // namespace met::readers::arl
