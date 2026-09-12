#include <algorithm>

#include <gtest/gtest.h>

#include "viewer/core/timeaxis.h"

using namespace met::core;

TEST(TimeAxis, TimegmUtcMatchesKnownEpochs) {
    EXPECT_EQ(timegmUtc(1970, 1, 1, 0, 0, 0), 0);
    EXPECT_EQ(timegmUtc(2000, 1, 1, 0, 0, 0), 946684800);
    EXPECT_EQ(timegmUtc(2020, 6, 1, 6, 0, 0), 1590991200);  // leap year, non-midnight
    EXPECT_EQ(timegmUtc(1969, 12, 31, 23, 59, 59), -1);     // just before the epoch
}

TEST(TimeAxis, FormatTimeMatchesCalendar) {
    EXPECT_EQ(formatTime(TimePoint{0}), "1970-01-01T00:00Z");
    EXPECT_EQ(formatTime(TimePoint{1590991200}), "2020-06-01T06:00Z");
    EXPECT_EQ(formatTime(TimePoint{timegmUtc(2024, 2, 29, 0, 0, 0)}), "2024-02-29T00:00Z");
    EXPECT_EQ(formatTime(TimePoint{timegmUtc(1985, 3, 31, 12, 30, 0)}), "1985-03-31T12:30Z");
}

// Every other assertion here sits at or after the epoch, which is how a
// positive-seconds test for "is there a time?" reached the export file names:
// nothing said what the calendar does when the seconds go negative. Reanalysis
// lives there routinely -- ERA5 reaches 1940, NCEP 1948.
TEST(TimeAxis, FormatTimeMatchesCalendarBeforeTheEpoch) {
    EXPECT_EQ(formatTime(TimePoint{-1}), "1969-12-31T23:59Z");
    EXPECT_EQ(formatTime(TimePoint{-86400}), "1969-12-31T00:00Z");
    EXPECT_EQ(formatTime(TimePoint{-86400 * 365}), "1969-01-01T00:00Z");  // 1969 is not a leap year

    // The eras the readers actually see, through timegmUtc so the pair are
    // checked against each other as well as against the calendar.
    EXPECT_EQ(formatTime(TimePoint{timegmUtc(1940, 1, 1, 0, 0, 0)}), "1940-01-01T00:00Z");
    EXPECT_EQ(formatTime(TimePoint{timegmUtc(1948, 1, 1, 6, 0, 0)}), "1948-01-01T06:00Z");
    // 1940 is a leap year; 1900 is not, being a century that 400 does not divide.
    EXPECT_EQ(formatTime(TimePoint{timegmUtc(1940, 2, 29, 18, 30, 0)}), "1940-02-29T18:30Z");
    EXPECT_EQ(formatTime(TimePoint{timegmUtc(1900, 3, 1, 0, 0, 0)}), "1900-03-01T00:00Z");
    EXPECT_EQ(timegmUtc(1900, 3, 1, 0, 0, 0) - timegmUtc(1900, 2, 28, 0, 0, 0), 86400);
}

// ISO-8601 basic format is exactly the extended one with its punctuation gone,
// so the two spellings are pinned against each other rather than against a
// hand-written table that could drift from formatTime.
TEST(TimeAxis, CompactTimeDropsOnlyTheSeparators) {
    EXPECT_EQ(compactTime(TimePoint{0}), "19700101T0000Z");
    EXPECT_EQ(compactTime(TimePoint{1590991200}), "20200601T0600Z");
    EXPECT_EQ(compactTime(TimePoint{timegmUtc(1985, 3, 31, 12, 30, 0)}), "19850331T1230Z");
    // Same instant as formatTime, verified against each other for a wide range.
    const std::int64_t span = 365LL * 24 * 3600;
    for (std::int64_t t = -2 * span; t <= 2 * span; t += 991) {
        std::string display = formatTime(TimePoint{t});
        display.erase(std::remove(display.begin(), display.end(), '-'), display.end());
        display.erase(std::remove(display.begin(), display.end(), ':'), display.end());
        EXPECT_EQ(compactTime(TimePoint{t}), display) << "t = " << t;
    }
}
