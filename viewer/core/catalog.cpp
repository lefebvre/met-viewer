#include "viewer/core/catalog.h"

#include <algorithm>

namespace met::core {

std::uint64_t makeCellKey(int levelIdx, int timeIdx, int member) {
    // Pack three small sorted-axis indices into one 64-bit key. 20 bits each
    // allows ~1M levels/times/members — far beyond any real dataset. Values are
    // masked, so a pathological count would alias to a wrong (existing) record
    // rather than corrupt memory; not guarded because it cannot occur in practice.
    const std::uint64_t l = static_cast<std::uint64_t>(levelIdx) & 0xFFFFF;         // 20 bits
    const std::uint64_t t = static_cast<std::uint64_t>(timeIdx) & 0xFFFFF;          // 20 bits
    const std::uint64_t m = static_cast<std::uint64_t>(member + 1) & 0xFFFFF;       // 20 bits
    return (l << 40) | (t << 20) | m;
}

// Exact-match lookup. `VerticalLevel::operator==` compares the double `value`
// bit-for-bit, so callers must resolve() with a level taken from this catalog's
// own axis (the UI does); a value reconstructed from a rounded display string may
// not match. Same contract applies to timeIndex().
std::optional<int> VariableEntry::levelIndex(const VerticalLevel& lvl) const {
    for (std::size_t i = 0; i < levels.size(); ++i) {
        if (levels[i] == lvl) return static_cast<int>(i);
    }
    return std::nullopt;
}

std::optional<int> VariableEntry::timeIndex(TimePoint t) const {
    for (std::size_t i = 0; i < times.size(); ++i) {
        if (times[i] == t) return static_cast<int>(i);
    }
    return std::nullopt;
}

std::optional<RecordHandle> VariableEntry::record(int levelIdx, int timeIdx, int member) const {
    const auto it = records.find(makeCellKey(levelIdx, timeIdx, member));
    if (it == records.end()) return std::nullopt;
    return it->second;
}

void DatasetCatalog::addRecord(const std::string& varName, const std::string& longName,
                               const std::string& units, const std::string& standardName,
                               const VerticalLevel& level, TimePoint validTime, int member,
                               RecordHandle handle) {
    finalized_ = false;

    VariableEntry* entry = nullptr;
    for (auto& v : variables_) {
        if (v.varName == varName) {
            entry = &v;
            break;
        }
    }
    if (!entry) {
        VariableEntry v;
        v.varName = varName;
        v.longName = longName;
        v.units = units;
        v.standardName = standardName;
        variables_.push_back(std::move(v));
        entry = &variables_.back();
        pending_.emplace_back();  // parallel raw-record bucket
    }

    // Index of this entry, to address the parallel pending bucket.
    const std::size_t idx = static_cast<std::size_t>(entry - variables_.data());
    pending_[idx].push_back(RawRecordStore{level, validTime, member, handle});
}

void DatasetCatalog::finalize() {
    if (finalized_) return;

    for (std::size_t vi = 0; vi < variables_.size(); ++vi) {
        VariableEntry& entry = variables_[vi];
        const auto& raws = pending_[vi];

        // Collect distinct axis values.
        entry.levels.clear();
        entry.times.clear();
        entry.members.clear();
        for (const auto& r : raws) {
            if (std::find(entry.levels.begin(), entry.levels.end(), r.level) == entry.levels.end())
                entry.levels.push_back(r.level);
            if (std::find(entry.times.begin(), entry.times.end(), r.time) == entry.times.end())
                entry.times.push_back(r.time);
            if (std::find(entry.members.begin(), entry.members.end(), r.member) ==
                entry.members.end())
                entry.members.push_back(r.member);
        }

        std::sort(entry.levels.begin(), entry.levels.end(),
                  [](const VerticalLevel& a, const VerticalLevel& b) {
                      return levelSortKey(a) < levelSortKey(b);
                  });
        std::sort(entry.times.begin(), entry.times.end());
        std::sort(entry.members.begin(), entry.members.end());

        // Build the sparse record map against sorted axis indices.
        entry.records.clear();
        for (const auto& r : raws) {
            const int li = static_cast<int>(
                std::find(entry.levels.begin(), entry.levels.end(), r.level) - entry.levels.begin());
            const int ti = static_cast<int>(
                std::find(entry.times.begin(), entry.times.end(), r.time) - entry.times.begin());
            // Last writer wins for a duplicate (var, level, time, member) cell.
            entry.records[makeCellKey(li, ti, r.member)] = r.handle;
        }
    }

    finalized_ = true;
}

const VariableEntry* DatasetCatalog::find(const std::string& varName) const {
    for (const auto& v : variables_) {
        if (v.varName == varName) return &v;
    }
    return nullptr;
}

std::optional<RecordHandle> DatasetCatalog::resolve(const FieldKey& key) const {
    const VariableEntry* entry = find(key.varName);
    if (!entry) return std::nullopt;
    const auto li = entry->levelIndex(key.level);
    const auto ti = entry->timeIndex(key.validTime);
    if (!li || !ti) return std::nullopt;
    return entry->record(*li, *ti, key.member);
}

}  // namespace met::core
