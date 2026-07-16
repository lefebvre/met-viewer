#include "viewer/readers/multidataset.h"

#include <cstddef>
#include <set>

namespace met::readers {

MultiDataset::MultiDataset(std::vector<std::shared_ptr<IDataset>> sources)
    : sources_(std::move(sources)) {
    if (sources_.empty()) throw ReadError("MultiDataset: no sources to merge");

    // Re-register every present record from every source into one catalog, tagging
    // it with its owning source index (stored as the opaque RecordHandle). The
    // catalog never inspects the handle, so the source index round-trips through
    // resolve() back to readField(). Level/time are re-added by value so the merged
    // axes hold bit-identical copies of what each source reported — routing then
    // re-resolves against the owning source with exactly that value.
    for (std::size_t si = 0; si < sources_.size(); ++si) {
        const auto handle = static_cast<core::RecordHandle>(si);
        for (const auto& var : sources_[si]->catalog().variables()) {
            for (std::size_t li = 0; li < var.levels.size(); ++li) {
                for (std::size_t ti = 0; ti < var.times.size(); ++ti) {
                    // `member` here is the raw member value (not an index): the
                    // records map is keyed by (levelIdx, timeIdx, memberValue).
                    for (int member : var.members) {
                        if (var.record(static_cast<int>(li), static_cast<int>(ti), member)) {
                            catalog_.addRecord(var.varName, var.longName, var.units,
                                               var.standardName, var.levels[li], var.times[ti],
                                               member, handle);
                        }
                    }
                }
            }
        }
    }
    catalog_.finalize();

    // "GRIB ×23" when homogeneous, "Mixed ×N" when sources differ in format.
    std::set<std::string> names;
    for (const auto& s : sources_) names.insert(s->formatName());
    const std::string base = names.size() == 1 ? *names.begin() : "Mixed";
    formatName_ =
        sources_.size() == 1 ? base : base + " ×" + std::to_string(sources_.size());
}

core::Field2D MultiDataset::readField(const core::FieldKey& key) {
    const auto handle = catalog_.resolve(key);
    if (!handle) throw ReadError("MultiDataset: field not present in dataset");
    const std::size_t idx = static_cast<std::size_t>(*handle);
    if (idx >= sources_.size()) throw ReadError("MultiDataset: record has invalid source index");
    return sources_[idx]->readField(key);
}

}  // namespace met::readers
