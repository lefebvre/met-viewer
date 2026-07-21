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

// Decode an ARL grid dimension. Dimensions above 999 don't fit the I3 field in
// the INDX header, so the two grid-ID characters in each record's label (bytes
// 12-13) carry the overflow: a grid-ID char >= 64 adds (char - 64) * 1000 to the
// corresponding dimension. HRRR's grid ID "AA" turns header 799/59 into
// 1799/1059. (See NOAA hysplitdata metdata.py.)
[[nodiscard]] int decodeGridDim(int headerValue, unsigned char gridIdChar);

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
