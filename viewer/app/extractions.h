#pragma once

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "viewer/analysis/crosssection.h"
#include "viewer/analysis/sounding.h"
#include "viewer/app/jobs.h"
#include "viewer/core/field.h"
#include "viewer/core/geo.h"
#include "viewer/core/timeaxis.h"
#include "viewer/readers/ireader.h"

namespace met::app {

// Multi-slab reads and the analysis extractions built on them.
//
// Everything here is a free function over an IDataset — no window state, no Qt
// widgets — because it all runs on the thread pool: a sounding over an HRRR file
// is ~40 slab decodes, which is not something the GUI thread can absorb. Keeping
// it out of MainWindow also makes it directly unit-testable against a fixture.
//
// Every read* takes an optional `onRead` invoked once per decoded slab (from the
// worker thread) so a job can drive a determinate progress bar. Slabs that fail to
// decode are logged and skipped rather than aborting the whole extraction — a
// missing level should cost you that level, not the sounding.
namespace extractions {

// All pressure levels of `varName` at `time`, as (pressure hPa, field).
[[nodiscard]] std::vector<std::pair<double, core::Field2D>> readLevelStack(
    readers::IDataset& ds, const std::string& varName, core::TimePoint time, int member,
    const std::function<void()>& onRead = {});

// All native model levels (hybrid/sigma) of `varName` at `time`, keyed by the
// model-level index rather than a pressure; pair with the `pres` field to place
// them on a real vertical axis.
[[nodiscard]] std::vector<std::pair<double, core::Field2D>> readModelLevelStack(
    readers::IDataset& ds, const std::string& varName, core::TimePoint time, int member,
    const std::function<void()>& onRead = {});

// All times of `varName` at `level`, as (time, field).
[[nodiscard]] std::vector<std::pair<core::TimePoint, core::Field2D>> readTimeStack(
    readers::IDataset& ds, const std::string& varName, core::VerticalLevel level, int member,
    const std::function<void()>& onRead = {});

// The U/V stacks at `time` (both empty when the dataset has no wind pair).
// `modelLevels` selects native model levels over pressure levels.
void readWindStacks(readers::IDataset& ds, core::TimePoint time, int member,
                    std::vector<std::pair<double, core::Field2D>>& uStack,
                    std::vector<std::pair<double, core::Field2D>>& vStack, bool modelLevels = false,
                    const std::function<void()>& onRead = {});

// Extract a sounding / cross-section, taking the pressure-level path when the
// variable has isobaric levels and otherwise the native model-level path (whose
// vertical axis comes from the `pres` field). When `progress` is set each slab
// read bumps its counter, and `generating` flips once the reads finish and the
// (unmeasured) extraction begins.
[[nodiscard]] analysis::Sounding computeSounding(readers::IDataset& ds, core::TimePoint time,
                                                 int member, core::LatLon point,
                                                 std::shared_ptr<JobProgress> progress = {});
[[nodiscard]] analysis::CrossSection computeCrossSection(
    readers::IDataset& ds, const std::string& var, core::TimePoint time, int member,
    const std::vector<core::LatLon>& path, int nSamples,
    std::shared_ptr<JobProgress> progress = {});

// How many slab reads the matching compute* will perform. Catalog-only (no I/O),
// so a progress bar can be sized before the job starts.
[[nodiscard]] int estimateSoundingReads(readers::IDataset& ds);
[[nodiscard]] int estimateCrossSectionReads(readers::IDataset& ds, const std::string& var);

}  // namespace extractions
}  // namespace met::app
