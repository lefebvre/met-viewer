#pragma once

#include <cstdint>
#include <map>
#include <optional>

namespace met::app {

// The bookkeeping every analysis panel that follows the time slider needs: the
// point profile, and each cross-section and Skew-T tab. One extraction runs at a
// time, a request that arrives meanwhile is chased when it lands rather than
// queued behind every intermediate one, and finished results are cached so a
// looping Play recomputes nothing after the first pass.
//
// It is one template because the panels used to carry three hand-written copies,
// and two bugs lived in all three:
//
// - A result was shown whenever it landed. Step to T1 (extraction starts), step
//   back to T0 (served from cache), and T1's numbers then replaced the panel while
//   the slider read T0. `wanted` records what the panel should be showing, and
//   only a result for that is displayed.
// - A result extracted from the previous dataset was cached and shown after
//   File > Open replaced it. `inFlightEpoch` records the dataset generation a job
//   was started against, and a mismatch on delivery drops the result.
template <typename Key, typename Result>
struct CoalescingCache {
    bool inFlight = false;
    bool pending = false;              // a request arrived while one was in flight
    std::uint64_t epoch = 0;           // the dataset generation `entries` describe
    std::uint64_t inFlightEpoch = 0;   // the generation the running job reads
    std::optional<Key> wanted;         // what the panel should be showing now
    std::map<Key, Result> entries;

    // A replaced dataset invalidates everything cached against the old one.
    void setEpoch(std::uint64_t generation) {
        if (epoch == generation) return;
        epoch = generation;
        entries.clear();
    }
};

enum class CacheAction {
    Serve,    // a cached result answers this request
    Wait,     // an extraction is already running; this one is chased when it lands
    Extract,  // start an extraction
};

template <typename Result>
struct CachePlan {
    CacheAction action = CacheAction::Extract;
    // Valid only when action is Serve, and only until the cache is next touched.
    const Result* cached = nullptr;
};

// Decide what to do with a request for `key`, and record what that implies.
template <typename Key, typename Result>
[[nodiscard]] CachePlan<Result> planRequest(CoalescingCache<Key, Result>& cache, const Key& key) {
    cache.wanted = key;
    if (const auto it = cache.entries.find(key); it != cache.entries.end())
        return {CacheAction::Serve, &it->second};
    if (cache.inFlight) {
        cache.pending = true;
        return {CacheAction::Wait, nullptr};
    }
    cache.inFlight = true;
    cache.inFlightEpoch = cache.epoch;
    return {CacheAction::Extract, nullptr};
}

struct CacheDelivery {
    bool display = false;  // the result is what the panel should be showing now
    bool again = false;    // run another request: one was queued, or this one is stale
};

// Take a finished extraction for `key`. An unusable result (the variable is not
// in this dataset, the point is off the grid) is not cached, but is still
// reported for display so the panel can stop showing what it had before.
template <typename Key, typename Result>
[[nodiscard]] CacheDelivery deliverResult(CoalescingCache<Key, Result>& cache, const Key& key,
                                          const Result& result, bool usable = true) {
    cache.inFlight = false;
    CacheDelivery out;
    out.again = cache.pending;
    cache.pending = false;
    if (cache.inFlightEpoch != cache.epoch) {
        // Read from a dataset that has since been replaced: neither cached nor
        // shown, and the current request still needs answering.
        out.again = true;
        return out;
    }
    if (usable) cache.entries[key] = result;
    out.display = cache.wanted && *cache.wanted == key;
    return out;
}

}  // namespace met::app
