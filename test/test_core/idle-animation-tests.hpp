#pragma once

#include <gtest/gtest.h>

#include "device/animation/idle-animation.hpp"
#include "utility-tests.hpp"

class IdleAnimationTests : public testing::Test {
public:
    static constexpr uint8_t FRAME_MS = 16;
    static constexpr int TRANSITION_FRAMES = 30;

    /** Starts the animation with LED 0 on FROM and LED 1 on TO, so LED 0 walks from FROM to TO. */
    void SetUp() override {
        SimpleTimer::setPlatformClock(&clock);
        AnimationConfig config;
        config.loop = true;
        config.speed = FRAME_MS;
        for (int i = 0; i < 9; i++) {
            config.initialState.setLEDPair(i, i == 1 ? TO : FROM, 65);
        }
        animation.init(config);
    }

    /** Leaves no clock behind for the next test. */
    void TearDown() override { SimpleTimer::setPlatformClock(nullptr); }

    /** Advances one frame and returns the colour LED 0 shows. */
    LEDColor nextFrameColor() {
        clock.advance(FRAME_MS + 1);
        return animation.animate().leftLights[0].color;
    }

    // Red and green fade down, blue fades up.
    static constexpr LEDColor FROM = LEDColor(200, 237, 0);
    static constexpr LEDColor TO = LEDColor(20, 200, 90);

    FakePlatformClock clock;
    IdleAnimation animation;
};

TEST_F(IdleAnimationTests, channelsFadeStraightToTheNextColor) {
    LEDColor previous = nextFrameColor();
    EXPECT_EQ(previous.red, FROM.red);
    EXPECT_EQ(previous.green, FROM.green);
    EXPECT_EQ(previous.blue, FROM.blue);

    for (int frame = 1; frame < TRANSITION_FRAMES; frame++) {
        const LEDColor color = nextFrameColor();
        EXPECT_LE(color.red, previous.red) << "frame " << frame;
        EXPECT_GE(color.red, TO.red) << "frame " << frame;
        EXPECT_LE(color.green, previous.green) << "frame " << frame;
        EXPECT_GE(color.green, TO.green) << "frame " << frame;
        EXPECT_GE(color.blue, previous.blue) << "frame " << frame;
        EXPECT_LE(color.blue, TO.blue) << "frame " << frame;
        if (frame == 1) {
            // 237 - 37/30: the step UBSan flagged at -1.23
            EXPECT_EQ(color.green, 236);
        }
        if (frame == TRANSITION_FRAMES / 2) {
            EXPECT_EQ(color.red, 110);
            EXPECT_EQ(color.green, 219);
            EXPECT_EQ(color.blue, 45);
        }
        previous = color;
    }

    const LEDColor arrived = nextFrameColor();
    EXPECT_EQ(arrived.red, TO.red);
    EXPECT_EQ(arrived.green, TO.green);
    EXPECT_EQ(arrived.blue, TO.blue);
}
