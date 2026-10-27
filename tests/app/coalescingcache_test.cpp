// The coalescing cache behind the cross-section and Skew-T tabs (the point
// profile's own tests cover the same template through its wrapper).

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <utility>

#include "viewer/app/coalescingcache.h"

using namespace met::app;

namespace {
using Key = std::pair<std::int64_t, int>;
using Tab = CoalescingCache<Key, std::string>;
constexpr Key kT0{0, -1};
constexpr Key kT1{3600, -1};
}  // namespace

TEST(CoalescingCache, ServesWhatItHasAndExtractsWhatItDoesNot) {
    Tab tab;
    EXPECT_EQ(planRequest(tab, kT0).action, CacheAction::Extract);
    const CacheDelivery d = deliverResult(tab, kT0, std::string("t0"));
    EXPECT_TRUE(d.display);
    EXPECT_FALSE(d.again);

    const auto plan = planRequest(tab, kT0);
    ASSERT_EQ(plan.action, CacheAction::Serve);
    EXPECT_EQ(*plan.cached, "t0");
}

TEST(CoalescingCache, ChasesTheNewestRequestOnceTheRunningOneLands) {
    Tab tab;
    ASSERT_EQ(planRequest(tab, kT0).action, CacheAction::Extract);
    EXPECT_EQ(planRequest(tab, kT1).action, CacheAction::Wait);

    const CacheDelivery d = deliverResult(tab, kT0, std::string("t0"));
    EXPECT_FALSE(d.display) << "the slider has moved on to T1";
    EXPECT_TRUE(d.again);
    EXPECT_EQ(planRequest(tab, kT1).action, CacheAction::Extract);
}

// After a dataset replace a variable may simply not be there. That result is not
// worth caching, but it is what the tab should now show instead of the old
// file's section.
TEST(CoalescingCache, ReportsAnUnusableResultForDisplayWithoutCachingIt) {
    Tab tab;
    ASSERT_EQ(planRequest(tab, kT0).action, CacheAction::Extract);
    const CacheDelivery d = deliverResult(tab, kT0, std::string(), /*usable=*/false);
    EXPECT_TRUE(d.display);
    EXPECT_TRUE(tab.entries.empty());
}

TEST(CoalescingCache, DropsAResultFromAReplacedDataset) {
    Tab tab;
    tab.setEpoch(1);
    ASSERT_EQ(planRequest(tab, kT0).action, CacheAction::Extract);
    tab.setEpoch(2);

    const CacheDelivery d = deliverResult(tab, kT0, std::string("old file"));
    EXPECT_FALSE(d.display);
    EXPECT_TRUE(d.again);
    EXPECT_TRUE(tab.entries.empty());
}
