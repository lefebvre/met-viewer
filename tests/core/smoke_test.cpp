#include <gtest/gtest.h>

#include "viewer/core/core.h"
#include "viewer/readers/readers.h"

TEST(Smoke, CoreLinksAndRuns) {
    EXPECT_GT(met::core::placeholder(), 0);
}

TEST(Smoke, ReadersLinkAndRun) {
    EXPECT_EQ(met::readers::placeholder(), 0);
}
