#pragma once

#include <filesystem>
#include <mutex>
#include <string>

#include "viewer/readers/ireader.h"

namespace met::readers::grib {

// An opened GRIB file. The catalog is built once at open time by scanning every
// message's metadata (headers only). readField() re-opens the file, seeks to the
// stored message byte offset, and decodes exactly that one message.
//
// Thread-safety: readField() takes an internal mutex, so concurrent calls are
// safe but serialized. (Decode is a fast per-message operation; the app-level
// FieldCache absorbs repeat reads.)
class GribDataset : public IDataset {
public:
    explicit GribDataset(std::filesystem::path path);

    [[nodiscard]] const core::DatasetCatalog& catalog() const override { return catalog_; }
    [[nodiscard]] core::Field2D readField(const core::FieldKey& key) override;
    [[nodiscard]] std::string formatName() const override { return "GRIB"; }

private:
    void scan();

    std::filesystem::path path_;
    core::DatasetCatalog catalog_;
    std::mutex mutex_;
};

}  // namespace met::readers::grib
