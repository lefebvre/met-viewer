#include <gtest/gtest.h>

#include "viewer/core/timeaxis.h"

using namespace met::core;

TEST(TimeAxis, TimegmUtcMatchesKnownEpochs) {
    EXPECT_EQ(timegmUtc(1970, 1, 1, 0, 0, 0), 0);
    EXPECT_EQ(timegmUtc(2000, 1, 1, 0, 0, 0), 946684800);
    EXPECT_EQ(timegmUtc(2020, 6, 1, 6, 0, 0), 1590991200);   // leap year, non-midnight
    EXPECT_EQ(timegmUtc(1969, 12, 31, 23, 59, 59), -1);      // just before the epoch
}

TEST(TimeAxis, FormatTimeMatchesCalendar) {
    EXPECT_EQ(formatTime(TimePoint{0}), "1970-01-01T00:00Z");
    EXPECT_EQ(formatTime(TimePoint{1590991200}), "2020-06-01T06:00Z");
    EXPECT_EQ(formatTime(TimePoint{timegmUtc(2024, 2, 29, 0, 0, 0)}), "2024-02-29T00:00Z");
    EXPECT_EQ(formatTime(TimePoint{timegmUtc(1985, 3, 31, 12, 30, 0)}), "1985-03-31T12:30Z");
}
