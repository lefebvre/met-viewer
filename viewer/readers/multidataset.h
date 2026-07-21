#pragma once

#include <memory>
#include <string>
#include <vector>

#include "viewer/readers/ireader.h"

namespace met::readers {

// An IDataset that aggregates several already-opened datasets into one. It merges
// their catalogs so a set of files that each hold a slice of a time series (e.g.
// HRRR's one-file-per-forecast-hour) presents as a single dataset with a unioned,
// sorted, de-duplicated time axis — which is exactly what the existing time
// slider, animation, and field cache already consume.
//
// Each merged record stores the index of its owning source as its (opaque)
// RecordHandle; readField() resolves the key against the merged catalog and
// delegates to that source, which does its own internal resolve/decode.
//
// Thread-safety: readField() only performs a const catalog lookup and delegates,
// and sources_/catalog_ are immutable after construction, so concurrent calls are
// as safe as the underlying sources (which each serialize their own reads). Reads
// of *different* sources proceed in parallel. The composite adds no shared mutable
// state and needs no mutex of its own.
class MultiDataset : public IDataset {
public:
    // Builds the merged catalog from the given sources (takes shared ownership so
    // callers may keep the leaves alive and rebuild the composite cheaply when
    // more files are added). Throws ReadError if `sources` is empty.
    explicit MultiDataset(std::vector<std::shared_ptr<IDataset>> sources);

    [[nodiscard]] const core::DatasetCatalog& catalog() const override { return catalog_; }
    [[nodiscard]] core::Field2D readField(const core::FieldKey& key) override;
    [[nodiscard]] std::string formatName() const override { return formatName_; }

private:
    std::vector<std::shared_ptr<IDataset>> sources_;
    core::DatasetCatalog catalog_;
    std::string formatName_;
};

}  // namespace met::readers
