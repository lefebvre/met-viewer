#include "viewer/app/extractions.h"

#include "viewer/analysis/heights.h"
#include "viewer/analysis/wind.h"
#include "viewer/core/log.h"

namespace met::app::extractions {
namespace {

// What every read* returns: levels (pressure or model-level index) paired with the
// decoded slab.
using Stack = std::vector<std::pair<double, core::Field2D>>;

// Count a variable's levels of the given kind, for sizing a progress bar without
// any I/O.
int countPressureLevels(const readers::IDataset& ds, const std::string& var) {
    const auto* entry = ds.catalog().find(var);
    if (!entry) return 0;
    int n = 0;
    for (const auto& lvl : entry->levels)
        if (lvl.type == core::VerticalLevel::Type::PressureHPa) ++n;
    return n;
}

int countModelLevels(const readers::IDataset& ds, const std::string& var) {
    const auto* entry = ds.catalog().find(var);
    if (!entry) return 0;
    int n = 0;
    for (const auto& lvl : entry->levels)
        if (lvl.type == core::VerticalLevel::Type::Hybrid ||
            lvl.type == core::VerticalLevel::Type::Sigma)
            ++n;
    return n;
}

// A per-slab tick bound to a job's counter (no-op when there is no job).
std::function<void()> readTick(const std::shared_ptr<JobProgress>& p) {
    if (!p) return {};
    return [p] { p->done.fetch_add(1, std::memory_order_relaxed); };
}

// Mark the transition from loading slabs to extracting the plot, so the bar shows
// a busy animation for that (unmeasured, worker-thread) phase.
void markGenerating(const std::shared_ptr<JobProgress>& p) {
    if (p) p->generating.store(true, std::memory_order_relaxed);
}

// Read every level of `varName` whose type passes `wanted`, keyed by `key`.
template <typename WantedFn, typename KeyFn>
std::vector<std::pair<double, core::Field2D>> readLevels(readers::IDataset& ds,
                                                         const std::string& varName,
                                                         core::TimePoint time, int member,
                                                         const std::function<void()>& onRead,
                                                         WantedFn wanted, KeyFn key) {
    std::vector<std::pair<double, core::Field2D>> stack;
    const auto* entry = ds.catalog().find(varName);
    if (!entry) return stack;
    for (const auto& lvl : entry->levels) {
        if (!wanted(lvl)) continue;
        try {
            stack.emplace_back(key(lvl), ds.readField(core::FieldKey{varName, lvl, time, member}));
        } catch (const std::exception& e) {
            // One bad level should cost that level, not the whole profile.
            MET_LOG_DEBUG("skipping {} at {}: {}", varName, core::formatLevel(lvl), e.what());
        }
        if (onRead) onRead();
    }
    return stack;
}

// The dataset's geopotential-height variable, or "" when it has none. Empty
// reads as "no height axis" everywhere below, since readLevels("") finds nothing.
std::string heightVar(const readers::IDataset& ds) {
    return analysis::findHeightVariable(ds.catalog().variables()).value_or(std::string{});
}

bool isPressure(const core::VerticalLevel& lvl) {
    return lvl.type == core::VerticalLevel::Type::PressureHPa;
}

bool isModelLevel(const core::VerticalLevel& lvl) {
    return lvl.type == core::VerticalLevel::Type::Hybrid ||
           lvl.type == core::VerticalLevel::Type::Sigma;
}

}  // namespace

std::vector<std::pair<double, core::Field2D>> readLevelStack(readers::IDataset& ds,
                                                             const std::string& varName,
                                                             core::TimePoint time, int member,
                                                             const std::function<void()>& onRead) {
    return readLevels(ds, varName, time, member, onRead, isPressure,
                      [](const core::VerticalLevel& lvl) { return lvl.value; });
}

std::vector<std::pair<double, core::Field2D>> readModelLevelStack(
    readers::IDataset& ds, const std::string& varName, core::TimePoint time, int member,
    const std::function<void()>& onRead) {
    return readLevels(ds, varName, time, member, onRead, isModelLevel,
                      [](const core::VerticalLevel& lvl) { return lvl.value; });
}

std::vector<std::pair<core::TimePoint, core::Field2D>> readTimeStack(
    readers::IDataset& ds, const std::string& varName, core::VerticalLevel level, int member,
    const std::function<void()>& onRead) {
    std::vector<std::pair<core::TimePoint, core::Field2D>> stack;
    const auto* entry = ds.catalog().find(varName);
    if (!entry) return stack;
    for (const auto& t : entry->times) {
        try {
            stack.emplace_back(t, ds.readField(core::FieldKey{varName, level, t, member}));
        } catch (const std::exception& e) {
            MET_LOG_DEBUG("skipping {} at {}: {}", varName, core::formatTime(t), e.what());
        }
        if (onRead) onRead();
    }
    return stack;
}

void readWindStacks(readers::IDataset& ds, core::TimePoint time, int member,
                    std::vector<std::pair<double, core::Field2D>>& uStack,
                    std::vector<std::pair<double, core::Field2D>>& vStack, bool modelLevels,
                    const std::function<void()>& onRead) {
    uStack.clear();
    vStack.clear();
    std::vector<std::string> names;
    for (const auto& v : ds.catalog().variables()) names.push_back(v.varName);
    const auto pair = analysis::findWindPair(names);
    if (!pair) return;  // no U/V pair -> no wind profile
    uStack = modelLevels ? readModelLevelStack(ds, pair->uName, time, member, onRead)
                         : readLevelStack(ds, pair->uName, time, member, onRead);
    vStack = modelLevels ? readModelLevelStack(ds, pair->vName, time, member, onRead)
                         : readLevelStack(ds, pair->vName, time, member, onRead);
}

analysis::Sounding computeSounding(readers::IDataset& ds, core::TimePoint time, int member,
                                   core::LatLon point, std::shared_ptr<JobProgress> progress) {
    const auto onRead = readTick(progress);
    // Isobaric path: temperature required; relative humidity (dewpoint) and U/V
    // (wind profile) optional.
    const std::string zVar = heightVar(ds);
    const auto tStack = readLevelStack(ds, "t", time, member, onRead);
    if (tStack.size() >= 2) {
        const auto rhStack = readLevelStack(ds, "r", time, member, onRead);
        std::vector<std::pair<double, core::Field2D>> uStack, vStack;
        readWindStacks(ds, time, member, uStack, vStack, /*modelLevels=*/false, onRead);
        const auto zStack = readLevelStack(ds, zVar, time, member, onRead);
        markGenerating(progress);
        return analysis::extractSounding(tStack, rhStack, point, uStack, vStack, zStack);
    }
    // Native model-level path: pressure comes from the `pres` field, dewpoint from
    // specific humidity `q`.
    const auto tModel = readModelLevelStack(ds, "t", time, member, onRead);
    const auto presStack = readModelLevelStack(ds, "pres", time, member, onRead);
    if (tModel.size() < 2 || presStack.size() < 2) {
        MET_LOG_DEBUG("no sounding: {} isobaric / {} model temperature levels, {} pressure levels",
                      tStack.size(), tModel.size(), presStack.size());
        return analysis::Sounding{};
    }
    const auto qStack = readModelLevelStack(ds, "q", time, member, onRead);
    std::vector<std::pair<double, core::Field2D>> uStack, vStack;
    readWindStacks(ds, time, member, uStack, vStack, /*modelLevels=*/true, onRead);
    const auto zStack = readModelLevelStack(ds, zVar, time, member, onRead);
    markGenerating(progress);
    return analysis::extractSoundingModelLevels(tModel, presStack, qStack, point, uStack, vStack,
                                                zStack);
}

analysis::CrossSection computeCrossSection(readers::IDataset& ds, const std::string& var,
                                           core::TimePoint time, int member,
                                           const std::vector<core::LatLon>& path, int nSamples,
                                           std::shared_ptr<JobProgress> progress) {
    const auto onRead = readTick(progress);
    // Height for the isopleth overlay. Sectioning the height field itself is a
    // reasonable thing to do, and re-reading the same slabs to overlay them on
    // themselves would double the I/O for nothing.
    const std::string zVar = heightVar(ds);
    const auto pres = readLevelStack(ds, var, time, member, onRead);
    if (pres.size() >= 2) {
        // Bound by reference either way: the stack holds every decoded slab, and
        // copying it to alias it under a second name would double the memory.
        const auto zRead = zVar == var ? Stack{} : readLevelStack(ds, zVar, time, member, onRead);
        const Stack& zStack = zVar == var ? pres : zRead;
        markGenerating(progress);
        return analysis::extractCrossSection(pres, path, nSamples, zStack);
    }
    // Native model-level path with a terrain-following pressure axis.
    const auto model = readModelLevelStack(ds, var, time, member, onRead);
    const auto presStack = readModelLevelStack(ds, "pres", time, member, onRead);
    if (model.size() < 2 || presStack.size() < 2) {
        MET_LOG_DEBUG("no cross-section for {}: {} isobaric / {} model levels, {} pressure levels",
                      var, pres.size(), model.size(), presStack.size());
        return analysis::CrossSection{};
    }
    const auto zRead = zVar == var ? Stack{} : readModelLevelStack(ds, zVar, time, member, onRead);
    const Stack& zStack = zVar == var ? model : zRead;
    markGenerating(progress);
    return analysis::extractCrossSectionModelLevels(model, presStack, path, nSamples, zStack);
}

int estimateSoundingReads(readers::IDataset& ds) {
    std::vector<std::string> names;
    for (const auto& v : ds.catalog().variables()) names.push_back(v.varName);
    const auto wind = analysis::findWindPair(names);
    const std::string uName = wind ? wind->uName : std::string{};
    const std::string vName = wind ? wind->vName : std::string{};
    const std::string zName = heightVar(ds);
    if (countPressureLevels(ds, "t") >= 2) {  // isobaric path
        return countPressureLevels(ds, "t") + countPressureLevels(ds, "r") +
               countPressureLevels(ds, uName) + countPressureLevels(ds, vName) +
               countPressureLevels(ds, zName);
    }
    return countModelLevels(ds, "t") + countModelLevels(ds, "pres") + countModelLevels(ds, "q") +
           countModelLevels(ds, uName) + countModelLevels(ds, vName) + countModelLevels(ds, zName);
}

int estimateCrossSectionReads(readers::IDataset& ds, const std::string& var) {
    const std::string zName = heightVar(ds);
    const int zPl = zName == var ? 0 : countPressureLevels(ds, zName);  // reused, not re-read
    const int pl = countPressureLevels(ds, var);
    if (pl >= 2) return pl + zPl;
    const int zMl = zName == var ? 0 : countModelLevels(ds, zName);
    return countModelLevels(ds, var) + countModelLevels(ds, "pres") + zMl;
}

}  // namespace met::app::extractions
