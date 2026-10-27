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

ProfilePlan planProfileRequest(PointProfileCache& cache, const ProfileCacheKey& key,
                               int estimatedReads) {
    ProfilePlan plan;
    plan.reads = estimatedReads;

    // Nothing to read is decided before the cache is consulted: a selection that
    // reads nothing has nothing to show, whatever an earlier one happened to
    // leave behind under the same key. Nothing is wanted either, so an
    // extraction still running for the previous selection does not replace the
    // "choose a column" state when it lands.
    if (estimatedReads <= 0) {
        cache.wanted.reset();
        plan.action = ProfileAction::NothingToRead;
        return plan;
    }

    const CachePlan<analysis::PointProfile> shared = planRequest(cache, key);
    plan.cached = shared.cached;
    switch (shared.action) {
        case CacheAction::Serve:
            plan.action = ProfileAction::Serve;
            break;
        case CacheAction::Wait:
            plan.action = ProfileAction::Wait;
            break;
        case CacheAction::Extract:
            plan.action = ProfileAction::Extract;
            break;
    }
    return plan;
}

ProfileDelivery deliverProfile(PointProfileCache& cache, const ProfileCacheKey& key,
                               core::LatLon extractedFor, const analysis::PointProfile& profile) {
    if (cache.point.lat != extractedFor.lat || cache.point.lon != extractedFor.lon) {
        // Superseded. Time and member need no such check -- they are in the key,
        // so a result is still the right answer for them.
        cache.inFlight = false;
        cache.pending = false;
        ProfileDelivery out;
        out.again = true;
        return out;
    }
    return deliverResult(cache, key, profile);
}

}  // namespace met::app
