#include "viewer/app/fieldcache.h"

#include <utility>

namespace met::app {

std::string FieldCache::keyString(const core::FieldKey& key) {
    // varName | levelType : levelValue | timeEpoch | member
    std::string s = key.varName;
    s += '|';
    s += std::to_string(static_cast<int>(key.level.type));
    s += ':';
    s += std::to_string(key.level.value);
    s += '|';
    s += std::to_string(key.validTime.epochSeconds);
    s += '|';
    s += std::to_string(key.member);
    return s;
}

std::shared_ptr<core::Field2D> FieldCache::get(const core::FieldKey& key) {
    const auto it = map_.find(keyString(key));
    if (it == map_.end()) return nullptr;
    // Move to front (most recently used).
    lru_.splice(lru_.begin(), lru_, it->second);
    return it->second->field;
}

bool FieldCache::contains(const core::FieldKey& key) const {
    return map_.find(keyString(key)) != map_.end();
}

void FieldCache::put(const core::FieldKey& key, std::shared_ptr<core::Field2D> field) {
    if (!field) return;
    const std::string ks = keyString(key);
    const std::size_t bytes = field->values.size() * sizeof(float);

    const auto it = map_.find(ks);
    if (it != map_.end()) {
        // Replace existing entry.
        sizeBytes_ -= it->second->bytes;
        it->second->field = std::move(field);
        it->second->bytes = bytes;
        sizeBytes_ += bytes;
        lru_.splice(lru_.begin(), lru_, it->second);
    } else {
        lru_.push_front(Entry{ks, std::move(field), bytes});
        map_[ks] = lru_.begin();
        sizeBytes_ += bytes;
    }
    evictToBudget();
}

void FieldCache::evictToBudget() {
    // Never evict the single most-recent entry even if it exceeds the budget.
    while (sizeBytes_ > budgetBytes_ && lru_.size() > 1) {
        Entry& back = lru_.back();
        sizeBytes_ -= back.bytes;
        map_.erase(back.key);
        lru_.pop_back();
    }
}

void FieldCache::setBudgetBytes(std::size_t bytes) {
    budgetBytes_ = bytes;
    evictToBudget();
}

void FieldCache::clear() {
    lru_.clear();
    map_.clear();
    sizeBytes_ = 0;
}

}  // namespace met::app
