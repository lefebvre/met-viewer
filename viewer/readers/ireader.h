#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

#include "viewer/core/catalog.h"
#include "viewer/core/field.h"

namespace met::readers {

// Thrown by readers on decode/open failure.
class ReadError : public std::runtime_error {
public:
    explicit ReadError(const std::string& what) : std::runtime_error(what) {}
};

// An opened dataset: a catalog of what it contains plus lazy slab access.
//
// Thread-safety: unless a concrete reader documents otherwise, callers must
// assume readField() is NOT safe to call concurrently on the same IDataset;
// each reader states its own contract (e.g. GRIB opens a per-call FILE*, NetCDF
// serializes with an internal mutex).
class IDataset {
public:
    virtual ~IDataset() = default;

    [[nodiscard]] virtual const core::DatasetCatalog& catalog() const = 0;

    // Decode exactly one 2D slab. Throws ReadError if the key is absent or the
    // record cannot be decoded.
    [[nodiscard]] virtual core::Field2D readField(const core::FieldKey& key) = 0;

    [[nodiscard]] virtual std::string formatName() const = 0;
};

// A format handler: cheaply probes bytes/paths and opens matching files.
class IFormatReader {
public:
    virtual ~IFormatReader() = default;

    // Confidence in [0, 100] that this reader can open the file. `head` is the
    // first few KB (may be shorter for tiny files). Return 0 to decline.
    [[nodiscard]] virtual int probe(std::span<const std::byte> head,
                                    std::string_view path) const = 0;

    // Open the file. Throws ReadError on failure.
    [[nodiscard]] virtual std::unique_ptr<IDataset> open(
        const std::filesystem::path& path) const = 0;

    [[nodiscard]] virtual std::string name() const = 0;
};

}  // namespace met::readers
