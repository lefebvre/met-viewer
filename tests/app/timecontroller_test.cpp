#include <gtest/gtest.h>

#include <QEventLoop>
#include <QObject>
#include <QStringList>
#include <QTimer>

#include "viewer/app/timecontroller.h"

using namespace met::app;

namespace {
// Spin the event loop for `ms` so queued QTimer timeouts (playback advances) fire.
void spin(int ms) {
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}
}  // namespace

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

// The core of the fix: playback is closed-loop. advanceFrame() moves ONE step and
// then waits; without a frameReady() ack it must not advance again (no fixed-timer
// "hail" of frames the owner can't keep up with).
TEST(TimeController, PlaybackWaitsForFrameReady) {
    TimeController tc;
    tc.setSteps({"a", "b", "c", "d"}, 0);
    tc.setFps(60);  // ~16 ms per shot
    tc.play();
    ASSERT_TRUE(tc.isPlaying());

    spin(60);
    EXPECT_EQ(tc.currentIndex(), 1);  // exactly one advance, then it waits

    spin(80);
    EXPECT_EQ(tc.currentIndex(), 1);  // no frameReady() -> does NOT advance again
    EXPECT_TRUE(tc.isPlaying());      // still "playing" though the single-shot timer is idle

    tc.frameReady();
    spin(60);
    EXPECT_EQ(tc.currentIndex(), 2);  // the ack schedules exactly the next advance

    tc.setSteps({"x"}, 0);  // stop the timer before teardown
}

// With each frame acked (simulating instant cache hits), playback advances
// repeatedly and wraps past the end — the loop keeps running.
TEST(TimeController, FrameReadyDrivesContinuousPlayback) {
    TimeController tc;
    tc.setSteps({"a", "b", "c", "d"}, 0);
    tc.setFps(60);
    int advances = 0;
    QObject::connect(&tc, &TimeController::indexChanged, [&](int) {
        ++advances;
        tc.frameReady();  // ack immediately
    });
    tc.play();
    spin(200);
    tc.pause();
    EXPECT_GE(advances, 3);        // advanced several steps (handshake keeps re-arming)
    EXPECT_FALSE(tc.isPlaying());  // pause() stops it
}
