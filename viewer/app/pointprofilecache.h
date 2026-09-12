#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "viewer/analysis/pointprofile.h"
#include "viewer/core/geo.h"
#include "viewer/core/timeaxis.h"

namespace met::app {

// Deciding what the point-profile panel should do next: serve a cached profile,
// wait behind the one already running, start an extraction, or say there is
// nothing to read. Plus what to do with a result when it lands.
//
// Free functions over plain data, in the shape openpipeline.h uses, because the
// alternative is what this replaced: the same decisions inline in a MainWindow
// method, where no test could reach them. Two bugs lived there unnoticed -- a
// profile extracted for a point the user had already left being cached as the
// new point's answer, and the read-cost label keeping a stale number when a
// cache hit returned before it was updated. Both are pinned by tests now, which
// is the whole reason this is a separate file.

// What a cached profile is stored under. The picked point is deliberately absent:
// keying on it would let twenty re-picks accumulate twenty profiles for every
// visited time, so a point change clears the cache instead (see setPoint). That
// absence is exactly why a result has to be checked against the point it was
// extracted for before it is stored -- see deliverProfile.
struct ProfileCacheKey {
    std::int64_t epochSeconds = 0;
    int member = -1;
    std::string columns;  // the selected ids joined; the set changes the result

    friend bool operator<(const ProfileCacheKey& a, const ProfileCacheKey& b) {
        return std::tie(a.epochSeconds, a.member, a.columns) <
               std::tie(b.epochSeconds, b.member, b.columns);
    }
    friend bool operator==(const ProfileCacheKey& a, const ProfileCacheKey& b) {
        return a.epochSeconds == b.epochSeconds && a.member == b.member && a.columns == b.columns;
    }
};

[[nodiscard]] ProfileCacheKey makeProfileCacheKey(core::TimePoint time, int member,
                                                  const std::vector<std::string>& columnIds);

// One panel's cached profiles and its in-flight bookkeeping.
struct PointProfileCache {
    bool inFlight = false;
    bool pending = false;
    std::uint64_t epoch = 0;  // the dataset generation these entries describe
    core::LatLon point{};
    std::map<ProfileCacheKey, analysis::PointProfile> entries;

    // A new point invalidates everything: none of it was sampled here. Compared
    // exactly, since the value either came from the same pick or a different one.
    void setPoint(core::LatLon p);
    // A replaced dataset invalidates everything for the same reason.
    void setEpoch(std::uint64_t generation);
};

enum class ProfileAction {
    NothingToRead,  // no column selected, or none this dataset can offer
    Serve,          // a cached profile answers this request
    Wait,           // an extraction is already running; this one queues behind it
    Extract,        // start an extraction
};

struct ProfilePlan {
    ProfileAction action = ProfileAction::NothingToRead;
    // Always the cost of the *current* selection, whatever the action. Returning
    // it even for a cache hit is the point: the read-cost label has to fall when
    // a column is de-selected, and de-selecting lands on a previously cached set.
    int reads = 0;
    // Valid only when action is Serve, and only until the cache is next touched.
    const analysis::PointProfile* cached = nullptr;
};

// Decide what to do, and record the bookkeeping that decision implies: Extract
// marks the cache in-flight, Wait marks it pending. `estimatedReads` is taken
// rather than computed so the count cannot be worked out after an early return.
[[nodiscard]] ProfilePlan planProfileRequest(PointProfileCache& cache, const ProfileCacheKey& key,
                                             int estimatedReads);

struct ProfileDelivery {
    bool display = false;  // the result answers the point the panel is on now
    bool again = false;    // run another extraction: superseded, or one queued behind
};

// Take a finished extraction. `extractedFor` is the point the job was started
// with; when the panel has since moved, the result is dropped rather than cached,
// because the key cannot tell the two points apart and storing it would make the
// new point read the old point's data for as long as the entry lives.
[[nodiscard]] ProfileDelivery deliverProfile(PointProfileCache& cache, const ProfileCacheKey& key,
                                             core::LatLon extractedFor,
                                             const analysis::PointProfile& profile);

}  // namespace met::app
