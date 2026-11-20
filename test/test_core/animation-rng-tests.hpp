#pragma once

#include <gtest/gtest.h>
#include <string>
#include <vector>

#include "game/quickdraw-resources.hpp"
#include "device/animation/bounty-win-animation.hpp"
#include "device/animation/hunter-win-animation.hpp"
#include "device/animation/lose-animation.hpp"
#include "utility-tests.hpp"

class AnimationRngTests : public testing::Test {
protected:
    void SetUp() override {
        fakeClock = new FakePlatformClock();
        SimpleTimer::setPlatformClock(fakeClock);
        fakeClock->setTime(1000);
    }

    void TearDown() override {
        SimpleTimer::setPlatformClock(nullptr);
        delete fakeClock;
        fakeClock = nullptr;
    }

    FakePlatformClock* fakeClock = nullptr;
};

// Exposes the frame step so a test can drive it without the frame timer.
template <typename Animation> class AnimationProbe : public Animation {
public:
    using Animation::onAnimate;
};

inline std::string describeFrame(const LEDState& state) {
    std::string text;
    auto appendLed = [&text](const LEDState::SingleLEDState& led) {
        text += std::to_string(led.color.red) + "," + std::to_string(led.color.green) + "," +
                std::to_string(led.color.blue) + "@" + std::to_string(led.brightness) + " ";
    };
    for (const LEDState::SingleLEDState& led : state.leftLights)
        appendLed(led);
    for (const LEDState::SingleLEDState& led : state.rightLights)
        appendLed(led);
    appendLed(state.transmitLight);
    return text;
}

// A copy starts with its original's generator state, so if each instance owns its
// generator the copy replays the original frame for frame. A generator shared across
// instances (one per process) hands the copy the draws after the original's instead.
template <typename Animation> void animationCopyReplaysItsOriginal(FakePlatformClock* clock) {
    const int frameCount = 200;
    const unsigned long startMs = 1000;
    const unsigned long frameMs = 16;

    AnimationProbe<Animation> original;
    AnimationConfig config{true, 16};
    original.init(config);
    AnimationProbe<Animation> copy = original;

    std::vector<std::string> originalFrames;
    for (int frame = 0; frame < frameCount; frame++) {
        clock->setTime(startMs + frame * frameMs);
        originalFrames.push_back(describeFrame(original.onAnimate()));
    }
    for (int frame = 0; frame < frameCount; frame++) {
        clock->setTime(startMs + frame * frameMs);
        ASSERT_EQ(describeFrame(copy.onAnimate()), originalFrames[frame]) << "frame " << frame;
    }
}
