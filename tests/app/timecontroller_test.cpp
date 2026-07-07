#include <gtest/gtest.h>

#include <QObject>
#include <QStringList>

#include "viewer/app/timecontroller.h"

using namespace met::app;

TEST(TimeController, SetStepsClampsCurrentIndex) {
    TimeController tc;
    tc.setSteps({"a", "b", "c", "d"}, 2);
    EXPECT_EQ(tc.currentIndex(), 2);
    tc.setSteps({"a", "b", "c"}, 99);  // beyond the end -> clamp to last
    EXPECT_EQ(tc.currentIndex(), 2);
    tc.setSteps({"a", "b", "c"}, -5);  // before the start -> clamp to 0
    EXPECT_EQ(tc.currentIndex(), 0);
}

TEST(TimeController, PlaybackGatedByStepCount) {
    TimeController tc;
    tc.setSteps({"only"}, 0);      // a single step
    tc.play();
    EXPECT_FALSE(tc.isPlaying());  // playback needs >= 2 steps

    tc.setSteps({"a", "b", "c"}, 0);
    tc.play();
    EXPECT_TRUE(tc.isPlaying());

    tc.setSteps({"x"}, 0);         // reducing to one step stops playback
    EXPECT_FALSE(tc.isPlaying());
}

TEST(TimeController, SetStepsDoesNotEmitIndexChanged) {
    TimeController tc;
    tc.setSteps({"a", "b", "c"}, 0);
    int count = 0;
    QObject::connect(&tc, &TimeController::indexChanged, [&count](int) { ++count; });
    tc.setSteps({"a", "b", "c", "d", "e"}, 3);  // reconfigure is signal-blocked
    EXPECT_EQ(count, 0);
    EXPECT_EQ(tc.currentIndex(), 3);
}
