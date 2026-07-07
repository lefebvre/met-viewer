#pragma once

#include <cstddef>
#include <list>
#include <memory>
#include <string>
#include <unordered_map>

#include "viewer/core/field.h"

namespace met::app {

// An LRU cache of decoded fields keyed by FieldKey, bounded by a byte budget.
// Lets time scrubbing and animation reuse already-decoded frames instead of
// re-reading them. Not thread-safe: use from the GUI thread only (decode jobs
// hand their result back on the GUI thread before insertion).
class FieldCache {
public:
    explicit FieldCache(std::size_t budgetBytes) : budgetBytes_(budgetBytes) {}

    [[nodiscard]] std::shared_ptr<core::Field2D> get(const core::FieldKey& key);
    void put(const core::FieldKey& key, std::shared_ptr<core::Field2D> field);
    [[nodiscard]] bool contains(const core::FieldKey& key) const;

    void setBudgetBytes(std::size_t bytes);
    void clear();

    [[nodiscard]] std::size_t sizeBytes() const { return sizeBytes_; }
    [[nodiscard]] std::size_t count() const { return map_.size(); }

    // Build the string key for a FieldKey (also used by tests).
    [[nodiscard]] static std::string keyString(const core::FieldKey& key);

private:
    struct Entry {
        std::string key;
        std::shared_ptr<core::Field2D> field;
        std::size_t bytes;
    };

    void evictToBudget();

    std::size_t budgetBytes_;
    std::size_t sizeBytes_ = 0;
    std::list<Entry> lru_;  // front = most recently used
    std::unordered_map<std::string, std::list<Entry>::iterator> map_;
};

}  // namespace met::app
