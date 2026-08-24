#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <QStringList>

#include "viewer/app/jobs.h"
#include "viewer/core/catalog.h"
#include "viewer/core/field.h"
#include "viewer/core/grid.h"
#include "viewer/readers/ireader.h"

// The file-open pipeline, deliberately free of window state.
//
// Deciding *which* paths to open and *opening* them are pure data operations, but
// they used to be private members of MainWindow, which made them unreachable from
// a test without constructing the whole widget shell. They are plain functions
// here so the interesting cases — de-duplication against an already-loaded set, a
// batch where some files fail, an empty catalog — can be tested directly.
//
// Everything here is safe to call from a worker thread: nothing touches a QWidget.
namespace met::app {

// A batch of opened files: the successfully opened (path, dataset) pairs in order,
// plus the paths that failed (skipped, not fatal, with the reason attached).
struct OpenBatch {
    std::vector<std::pair<std::filesystem::path, std::shared_ptr<readers::IDataset>>> opened;
    QStringList skipped;
    // Each opened dataset's grid, sampled on the worker thread (nullopt when no
    // field could be read). Determining this needs a real slab decode, so it
    // happens here rather than on the GUI thread while installing the batch.
    std::vector<std::optional<core::GridDef>> grids;
};

// A FieldKey for the first available record of a catalog (first variable, first
// level/time/member), or nullopt if the catalog is empty.
[[nodiscard]] std::optional<core::FieldKey> firstFieldKey(const core::DatasetCatalog& cat);

// The grid of a dataset's first field, or nullopt if it has no readable field.
// Datasets don't expose a grid directly (a GRIB file may even hold several), so
// this reads one representative field and takes its grid. Best-effort: a decode
// error yields nullopt and the caller treats compatibility as unknown.
[[nodiscard]] std::optional<core::GridDef> representativeGrid(readers::IDataset& ds);

// The paths a request should actually open, given what is already loaded. For an
// add (`replace` false) this drops paths already in `alreadyLoaded`; either way it
// de-dupes the request and sorts, so the load order is deterministic (HRRR hourly
// filenames sort chronologically).
[[nodiscard]] std::vector<std::filesystem::path> pathsToOpen(
    const QStringList& requested, const std::vector<std::filesystem::path>& alreadyLoaded,
    bool replace);

// Open each path, catching per-file failures so one bad file does not lose the
// batch; bumps progress->done per file when given.
[[nodiscard]] OpenBatch openBatch(const std::vector<std::filesystem::path>& paths,
                                  JobProgress* progress);

}  // namespace met::app
