#include "viewer/app/openpipeline.h"

#include <algorithm>
#include <exception>
#include <set>

#include "viewer/core/log.h"
#include "viewer/readers/detect.h"

namespace met::app {

std::optional<core::FieldKey> firstFieldKey(const core::DatasetCatalog& cat) {
    const auto& vars = cat.variables();
    if (vars.empty() || vars.front().levels.empty()) return std::nullopt;
    const auto& v = vars.front();
    core::FieldKey k;
    k.varName = v.varName;
    k.level = v.levels.front();
    k.validTime = v.times.empty() ? core::TimePoint{} : v.times.front();
    k.member = v.members.empty() ? -1 : v.members.front();
    return k;
}

std::optional<core::GridDef> representativeGrid(readers::IDataset& ds) {
    const auto key = firstFieldKey(ds.catalog());
    if (!key) return std::nullopt;
    try {
        return ds.readField(*key).grid;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::vector<std::filesystem::path> pathsToOpen(
    const QStringList& requested, const std::vector<std::filesystem::path>& alreadyLoaded,
    bool replace) {
    std::set<std::filesystem::path> existing;
    if (!replace)
        for (const auto& p : alreadyLoaded) existing.insert(p);

    std::set<std::filesystem::path> seen;
    std::vector<std::filesystem::path> out;
    for (const QString& p : requested) {
        if (p.isEmpty()) continue;
        std::filesystem::path fp(p.toStdString());
        if (existing.count(fp) || !seen.insert(fp).second) continue;
        out.push_back(std::move(fp));
    }
    std::sort(out.begin(), out.end());
    return out;
}

OpenBatch openBatch(const std::vector<std::filesystem::path>& paths, JobProgress* progress) {
    OpenBatch batch;
    for (const auto& path : paths) {
        try {
            auto ds = std::shared_ptr<readers::IDataset>(readers::openDataset(path));
            // Sample the grid here, on the worker thread: it costs a full slab
            // decode per file, which is exactly what should not happen on the GUI
            // thread while installing a batch.
            batch.grids.push_back(representativeGrid(*ds));
            batch.opened.emplace_back(path, std::move(ds));
        } catch (const std::exception& e) {
            // Keep the reason (e.g. "no reader recognizes file") so the skipped list
            // tells the user why, not just which.
            MET_LOG_WARN("could not open {}: {}", path.string(), e.what());
            batch.skipped << (QString::fromStdString(path.filename().string()) + ": " +
                              QString::fromUtf8(e.what()));
        }
        if (progress) progress->done.fetch_add(1, std::memory_order_relaxed);
    }
    return batch;
}

}  // namespace met::app
