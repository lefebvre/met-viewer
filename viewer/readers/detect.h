#pragma once

#include <filesystem>
#include <memory>
#include <vector>

#include "viewer/readers/ireader.h"

namespace met::readers {

// Opens a file by probing all registered format readers and using the
// highest-confidence match. Throws ReadError if no reader claims the file or the
// winning reader fails to open it.
[[nodiscard]] std::unique_ptr<IDataset> openDataset(const std::filesystem::path& path);

// Result of opening a set of files: the merged dataset plus any paths that failed
// to open (skipped rather than aborting the whole load).
struct OpenResult {
    std::shared_ptr<IDataset> dataset;
    std::vector<std::filesystem::path> skipped;
};

// Opens several files and merges them into one dataset whose time axis is the
// union of the inputs' (via MultiDataset). Paths that fail to open are collected
// in `skipped` instead of aborting; ReadError is thrown only if *every* path
// fails (or `paths` is empty). A single successful path is returned unwrapped, so
// single-file behavior (and formatName) is unchanged. This is the synchronous
// convenience used by the CLI and tests; the app opens files incrementally on a
// background thread to report progress and keep the sources for later "add".
[[nodiscard]] OpenResult openDatasets(const std::vector<std::filesystem::path>& paths);

}  // namespace met::readers
