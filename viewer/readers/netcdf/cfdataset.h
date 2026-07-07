#pragma once

#include <filesystem>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "viewer/core/grid.h"
#include "viewer/readers/ireader.h"

namespace met::readers::netcdf {

// A NetCDF/HDF5 dataset following CF conventions (covers ERA5 pressure-level and
// single-level downloads). Coordinate variables are detected by units /
// standard_name / name; packed data (scale_factor / add_offset / _FillValue) is
// unpacked to float with missing values normalized to NaN.
//
// Thread-safety: netcdf-c/HDF5 are not thread-safe here, so every library call
// is serialized behind an internal mutex.
class CfDataset : public IDataset {
public:
    explicit CfDataset(std::filesystem::path path);
    ~CfDataset() override;

    [[nodiscard]] const core::DatasetCatalog& catalog() const override { return catalog_; }
    [[nodiscard]] core::Field2D readField(const core::FieldKey& key) override;
    [[nodiscard]] std::string formatName() const override { return "NetCDF/CF"; }

private:
    // Per data-variable layout needed to read a 2D slab and unpack it.
    struct VarInfo {
        int varid = -1;
        int ndims = 0;
        // Position of each axis within the variable's dimension list, or -1.
        int timeAxis = -1;
        int levelAxis = -1;
        int latAxis = -1;
        int lonAxis = -1;
        double scale = 1.0;
        double offset = 0.0;
        double fill = 0.0;
        bool hasScale = false;
        bool hasOffset = false;
        bool hasFill = false;
        std::string units;
        std::string longName;
        std::string standardName;
    };

    void scan();

    std::filesystem::path path_;
    int ncid_ = -1;
    core::DatasetCatalog catalog_;
    core::GridDef grid_{core::RegularLatLonGrid{}};
    std::map<std::string, VarInfo> vars_;  // by canonical variable name
    std::mutex mutex_;
};

}  // namespace met::readers::netcdf
