#include "viewer/app/pointprofilecache.h"

#include <tuple>

namespace met::app {

ProfileCacheKey makeProfileCacheKey(core::TimePoint time, int member,
                                    const std::vector<std::string>& columnIds) {
    ProfileCacheKey key;
    key.epochSeconds = static_cast<std::int64_t>(time.epochSeconds);
    key.member = member;
    // Joined with a separator that cannot appear in a variable name, so {"ab"}
    // and {"a","b"} cannot collide into the same key.
    for (const std::string& id : columnIds) key.columns += id + "|";
    return key;
}

void PointProfileCache::setPoint(core::LatLon p) {
    if (point.lat == p.lat && point.lon == p.lon) return;
    point = p;
    entries.clear();
}

void PointProfileCache::setEpoch(std::uint64_t generation) {
    if (epoch == generation) return;
    epoch = generation;
    entries.clear();
}

ProfilePlan planProfileRequest(PointProfileCache& cache, const ProfileCacheKey& key,
                               int estimatedReads) {
    ProfilePlan plan;
    plan.reads = estimatedReads;

    // Nothing to read is decided before the cache is consulted: a selection that
    // reads nothing has nothing to show, whatever an earlier one happened to
    // leave behind under the same key.
    if (estimatedReads <= 0) {
        plan.action = ProfileAction::NothingToRead;
        return plan;
    }

    if (const auto it = cache.entries.find(key); it != cache.entries.end()) {
        plan.action = ProfileAction::Serve;
        plan.cached = &it->second;
        return plan;
    }

    if (cache.inFlight) {
        // Coalesce: one extraction at a time, and the newest request is chased
        // when it lands rather than queued behind every intermediate one.
        cache.pending = true;
        plan.action = ProfileAction::Wait;
        return plan;
    }

    cache.inFlight = true;
    plan.action = ProfileAction::Extract;
    return plan;
}

ProfileDelivery deliverProfile(PointProfileCache& cache, const ProfileCacheKey& key,
                               core::LatLon extractedFor, const analysis::PointProfile& profile) {
    cache.inFlight = false;
    ProfileDelivery out;

    if (cache.point.lat != extractedFor.lat || cache.point.lon != extractedFor.lon) {
        // Superseded. Time and member need no such check -- they are in the key,
        // so a result is still the right answer for them.
        cache.pending = false;
        out.again = true;
        return out;
    }

    cache.entries[key] = profile;
    out.display = true;
    out.again = cache.pending;
    cache.pending = false;
    return out;
}

}  // namespace met::app
