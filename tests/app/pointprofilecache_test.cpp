// The decisions the point-profile panel makes about its cache, which used to
// live inline in a MainWindow method where no test could reach them. Two bugs
// got through that way and are pinned below: a profile extracted for a point the
// user had already left being cached as the new point's answer, and the read-cost
// label keeping a stale number when a cache hit returned before it was updated.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "viewer/analysis/pointprofile.h"
#include "viewer/app/pointprofilecache.h"

using namespace met;
using namespace met::app;

namespace {

constexpr core::TimePoint kNoon{1714564800};   // 2024-05-01T12:00Z
constexpr core::TimePoint kLater{1714572000};  // three hours on
constexpr core::LatLon kDenver{39.74, -104.98};
constexpr core::LatLon kBoulder{40.01, -105.27};

// A profile distinguishable by the point it claims, so a test can tell which
// extraction a served result came from.
analysis::PointProfile profileAt(core::LatLon point) {
    analysis::PointProfile p;
    p.point = point;
    p.validTime = kNoon;
    p.pointInDomain = true;
    p.columns = {{"t", "Temperature", "K", analysis::ProfileColumnKind::Variable}};
    analysis::ProfileLevel row;
    row.level = {core::VerticalLevel::Type::PressureHPa, 500.0};
    row.values = {250.0f};
    row.statuses = {analysis::CellStatus::Ok};
    p.levels = {row};
    return p;
}

ProfileCacheKey key(const std::vector<std::string>& columns, core::TimePoint t = kNoon) {
    return makeProfileCacheKey(t, -1, columns);
}

}  // namespace

TEST(ProfileCacheKey, SeparatesColumnIdsSoTwoSelectionsCannotCollide) {
    // {"ab"} and {"a","b"} would be one string if the ids were simply run
    // together, and the two selections read different data.
    EXPECT_FALSE(key({"ab"}) == key({"a", "b"}));
    EXPECT_TRUE(key({"t", "r"}) == key({"t", "r"}));
    EXPECT_FALSE(key({"t", "r"}) == key({"r", "t"}));
    EXPECT_FALSE(key({"t"}) == key({"t"}, kLater));
    EXPECT_FALSE(makeProfileCacheKey(kNoon, -1, {"t"}) == makeProfileCacheKey(kNoon, 2, {"t"}));
}

// The reported bug: adding a column raised the read count, and de-selecting it
// left the higher number on screen. De-selecting lands on a previously cached
// set, so the count has to come back whatever the cache says.
TEST(PlanProfileRequest, ReportsTheCurrentSelectionsReadCountEvenWhenServingFromCache) {
    PointProfileCache cache;
    cache.setPoint(kDenver);

    // Extract and deliver the narrow selection, so it is cached.
    ASSERT_EQ(planProfileRequest(cache, key({"t"}), 9).action, ProfileAction::Extract);
    ASSERT_TRUE(deliverProfile(cache, key({"t"}), kDenver, profileAt(kDenver)).display);

    // Widen: a miss, and the higher cost.
    const ProfilePlan wide = planProfileRequest(cache, key({"t", "r"}), 18);
    EXPECT_EQ(wide.action, ProfileAction::Extract);
    EXPECT_EQ(wide.reads, 18);
    ASSERT_TRUE(deliverProfile(cache, key({"t", "r"}), kDenver, profileAt(kDenver)).display);

    // Narrow again: served from cache, and the count falls with the selection.
    const ProfilePlan narrow = planProfileRequest(cache, key({"t"}), 9);
    EXPECT_EQ(narrow.action, ProfileAction::Serve);
    EXPECT_EQ(narrow.reads, 9) << "the label must follow the selection, not the cache";
    ASSERT_NE(narrow.cached, nullptr);
}

// The reported bug: pick a point, re-pick while it extracts, and the first
// point's profile was stored under a key that does not mention the point. Every
// later lookup then served it as the new point's answer, so the panel never
// re-extracted and the wrong data persisted.
TEST(DeliverProfile, DropsAResultForAPointThePanelHasLeftInsteadOfCachingIt) {
    PointProfileCache cache;
    cache.setPoint(kDenver);
    ASSERT_EQ(planProfileRequest(cache, key({"t"}), 9).action, ProfileAction::Extract);

    // The user re-picks while that extraction is running.
    cache.setPoint(kBoulder);

    const ProfileDelivery delivered =
        deliverProfile(cache, key({"t"}), kDenver, profileAt(kDenver));
    EXPECT_FALSE(delivered.display) << "a profile for the old point must not be shown";
    EXPECT_TRUE(delivered.again) << "the new point still needs extracting";
    EXPECT_TRUE(cache.entries.empty()) << "and it must not be cached as the new point's answer";
    EXPECT_FALSE(cache.inFlight);

    // The new point therefore extracts rather than being served the stale result.
    EXPECT_EQ(planProfileRequest(cache, key({"t"}), 9).action, ProfileAction::Extract);
}

TEST(DeliverProfile, CachesAndShowsAResultForThePointStillPickedOn) {
    PointProfileCache cache;
    cache.setPoint(kDenver);
    ASSERT_EQ(planProfileRequest(cache, key({"t"}), 9).action, ProfileAction::Extract);

    const ProfileDelivery delivered =
        deliverProfile(cache, key({"t"}), kDenver, profileAt(kDenver));
    EXPECT_TRUE(delivered.display);
    EXPECT_FALSE(delivered.again) << "nothing was queued behind it";
    EXPECT_EQ(cache.entries.size(), 1u);

    const ProfilePlan again = planProfileRequest(cache, key({"t"}), 9);
    ASSERT_EQ(again.action, ProfileAction::Serve);
    EXPECT_NEAR(again.cached->point.lat, kDenver.lat, 1e-9);
}

TEST(PlanProfileRequest, QueuesASecondRequestBehindTheRunningOneRatherThanStartingIt) {
    PointProfileCache cache;
    cache.setPoint(kDenver);
    ASSERT_EQ(planProfileRequest(cache, key({"t"}), 9).action, ProfileAction::Extract);

    // A time change while that runs.
    const ProfilePlan second = planProfileRequest(cache, key({"t"}, kLater), 9);
    EXPECT_EQ(second.action, ProfileAction::Wait);
    EXPECT_TRUE(cache.pending);

    // When the first lands it is cached, and the queued request is chased.
    const ProfileDelivery delivered =
        deliverProfile(cache, key({"t"}), kDenver, profileAt(kDenver));
    EXPECT_TRUE(delivered.display);
    EXPECT_TRUE(delivered.again);
    EXPECT_FALSE(cache.pending) << "the queued request is taken, not left to fire twice";
}

// A superseded result clears the queue too: whatever was pending was for a point
// nobody is on any more, and the re-run below covers the current one.
TEST(DeliverProfile, ClearsAQueuedRequestWhenTheResultItWaitedOnWasSuperseded) {
    PointProfileCache cache;
    cache.setPoint(kDenver);
    ASSERT_EQ(planProfileRequest(cache, key({"t"}), 9).action, ProfileAction::Extract);
    ASSERT_EQ(planProfileRequest(cache, key({"t"}, kLater), 9).action, ProfileAction::Wait);
    cache.setPoint(kBoulder);

    const ProfileDelivery delivered =
        deliverProfile(cache, key({"t"}), kDenver, profileAt(kDenver));
    EXPECT_TRUE(delivered.again);
    EXPECT_FALSE(cache.pending);
}

TEST(PlanProfileRequest, ReadsNothingWhenNoColumnIsSelected) {
    PointProfileCache cache;
    cache.setPoint(kDenver);
    const ProfilePlan plan = planProfileRequest(cache, key({}), 0);
    EXPECT_EQ(plan.action, ProfileAction::NothingToRead);
    EXPECT_EQ(plan.reads, 0);
    EXPECT_FALSE(cache.inFlight) << "nothing to read must not mark an extraction running";
}

// A zero-read selection has nothing to show whatever an earlier one left under
// the same key, so the cache is not consulted first.
TEST(PlanProfileRequest, PrefersNothingToReadOverAStaleEntryUnderTheSameKey) {
    PointProfileCache cache;
    cache.setPoint(kDenver);
    ASSERT_EQ(planProfileRequest(cache, key({"t"}), 9).action, ProfileAction::Extract);
    ASSERT_TRUE(deliverProfile(cache, key({"t"}), kDenver, profileAt(kDenver)).display);

    EXPECT_EQ(planProfileRequest(cache, key({"t"}), 0).action, ProfileAction::NothingToRead);
}

TEST(PointProfileCache, ForgetsEverythingWhenThePointMoves) {
    PointProfileCache cache;
    cache.setPoint(kDenver);
    ASSERT_EQ(planProfileRequest(cache, key({"t"}), 9).action, ProfileAction::Extract);
    ASSERT_TRUE(deliverProfile(cache, key({"t"}), kDenver, profileAt(kDenver)).display);
    ASSERT_EQ(cache.entries.size(), 1u);

    cache.setPoint(kBoulder);
    EXPECT_TRUE(cache.entries.empty());

    // Re-picking the same point is not a move, so it keeps what it has.
    ASSERT_EQ(planProfileRequest(cache, key({"t"}), 9).action, ProfileAction::Extract);
    ASSERT_TRUE(deliverProfile(cache, key({"t"}), kBoulder, profileAt(kBoulder)).display);
    cache.setPoint(kBoulder);
    EXPECT_EQ(cache.entries.size(), 1u);
}

TEST(PointProfileCache, ForgetsEverythingWhenTheDatasetIsReplaced) {
    PointProfileCache cache;
    cache.setEpoch(1);
    cache.setPoint(kDenver);
    ASSERT_EQ(planProfileRequest(cache, key({"t"}), 9).action, ProfileAction::Extract);
    ASSERT_TRUE(deliverProfile(cache, key({"t"}), kDenver, profileAt(kDenver)).display);

    cache.setEpoch(1);  // same dataset: nothing to forget
    EXPECT_EQ(cache.entries.size(), 1u);

    cache.setEpoch(2);
    EXPECT_TRUE(cache.entries.empty());
}
