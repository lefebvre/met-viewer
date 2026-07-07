#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "viewer/core/field.h"
#include "viewer/core/timeaxis.h"

namespace met::core {

// Opaque reference to one record in a file. The reader that produced the catalog
// interprets it (GRIB message byte offset, NetCDF variable+indices, ARL record
// number). The catalog itself never inspects it.
using RecordHandle = std::uint64_t;

// Per-variable index: the distinct level and time axes present, plus a sparse
// map from (levelIndex, timeIndex, member) to the record that holds that slab.
struct VariableEntry {
    std::string varName;
    std::string longName;
    std::string units;
    std::string standardName;

    std::vector<VerticalLevel> levels;  // sorted by levelSortKey
    std::vector<TimePoint> times;       // sorted ascending
    std::vector<int> members;           // sorted ascending; {-1} if deterministic

    // Key packs (levelIndex, timeIndex, member) — see makeCellKey.
    std::map<std::uint64_t, RecordHandle> records;

    [[nodiscard]] std::optional<int> levelIndex(const VerticalLevel& lvl) const;
    [[nodiscard]] std::optional<int> timeIndex(TimePoint t) const;
    [[nodiscard]] std::optional<RecordHandle> record(int levelIdx, int timeIdx, int member) const;
};

// A whole-file catalog built once at open time from metadata only.
class DatasetCatalog {
public:
    // Registers one record. Called by readers during their open-time scan. The
    // level/time/member axes are grown as new values appear; the caller need not
    // pre-sort. Call finalize() once all records are added.
    void addRecord(const std::string& varName, const std::string& longName,
                   const std::string& units, const std::string& standardName,
                   const VerticalLevel& level, TimePoint validTime, int member,
                   RecordHandle handle);

    // Sorts axes and remaps record keys to the sorted axis indices. Idempotent.
    void finalize();

    [[nodiscard]] const std::vector<VariableEntry>& variables() const { return variables_; }
    [[nodiscard]] const VariableEntry* find(const std::string& varName) const;

    // Resolve a FieldKey to a record handle, if present.
    [[nodiscard]] std::optional<RecordHandle> resolve(const FieldKey& key) const;

private:
    // One record as reported by a reader before axes are sorted.
    struct RawRecordStore {
        VerticalLevel level;
        TimePoint time;
        int member;
        RecordHandle handle;
    };

    std::vector<VariableEntry> variables_;
    // Parallel to variables_: raw records awaiting finalize().
    std::vector<std::vector<RawRecordStore>> pending_;
    bool finalized_ = false;
};

// Packs three small indices into one map key. Members are offset by 1 so the
// deterministic sentinel (-1) packs as 0.
[[nodiscard]] std::uint64_t makeCellKey(int levelIdx, int timeIdx, int member);

}  // namespace met::core
