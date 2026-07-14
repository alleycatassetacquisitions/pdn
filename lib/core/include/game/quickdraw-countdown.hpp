#pragma once

#include "device/animation/animation-base.hpp"
#include <cstddef>
#include "image.hpp"

namespace QuickdrawCountdown {

enum class Step : uint8_t {
    THREE = 3,
    TWO = 2,
    ONE = 1,
    DRAW = 0,
};

constexpr unsigned long kStepDurationMs = 2000;
constexpr unsigned long kReadyDurationMs = 2000;
constexpr unsigned long kOutcomeDurationMs = 3000;
constexpr unsigned long kRoundResultDurationMs = 2000;
constexpr unsigned long kDuelTimeoutMs = 4000;
constexpr uint8_t kFdnPracticeBrightness = static_cast<uint8_t>(255 * 35 / 100);
constexpr int kHapticDurationMs = 75;
constexpr int kHapticIntensity = 255;

inline LEDState makeLitBar(int startLed, int endLedExclusive) {
    LEDState state;
    for (int i = startLed; i < endLedExclusive; i++) {
        state.leftLights[i] = LEDState::SingleLEDState(LEDColor(255, 255, 255), 255);
        state.rightLights[i] = LEDState::SingleLEDState(LEDColor(255, 255, 255), 255);
    }
    return state;
}

inline const LEDState& threeLedState() {
    static const LEDState kState = LEDState();
    return kState;
}

inline const LEDState& twoLedState() {
    static const LEDState kState = makeLitBar(3, 5);
    return kState;
}

inline const LEDState& oneLedState() {
    static const LEDState kState = makeLitBar(3, 7);
    return kState;
}

inline const LEDState& drawLedState() {
    static const LEDState kState = makeLitBar(3, 9);
    return kState;
}

// FDN fin + recess strips: 9 LEDs each, fill left-to-right (0-8).
inline const LEDState& fdnThreeLedState() {
    static const LEDState kState = LEDState();
    return kState;
}

inline const LEDState& fdnTwoLedState() {
    static const LEDState kState = makeLitBar(0, 3);
    return kState;
}

inline const LEDState& fdnOneLedState() {
    static const LEDState kState = makeLitBar(0, 6);
    return kState;
}

inline const LEDState& fdnDrawLedState() {
    static const LEDState kState = makeLitBar(0, 9);
    return kState;
}

inline AnimationConfig fdnAnimationConfigFor(Step step) {
    AnimationConfig config;
    config.loop = false;
    config.speed = 16;
    config.loopDelayMs = 0;

    switch (step) {
        case Step::THREE:
            config.initialState = fdnThreeLedState();
            break;
        case Step::TWO:
            config.initialState = fdnTwoLedState();
            break;
        case Step::ONE:
            config.initialState = fdnOneLedState();
            break;
        case Step::DRAW:
            config.initialState = fdnDrawLedState();
            break;
    }

    return config;
}

inline ImageType imageTypeForStep(Step step) {
    switch (step) {
        case Step::THREE:
            return ImageType::COUNTDOWN_THREE;
        case Step::TWO:
            return ImageType::COUNTDOWN_TWO;
        case Step::ONE:
            return ImageType::COUNTDOWN_ONE;
        case Step::DRAW:
            return ImageType::DRAW;
    }
    return ImageType::COUNTDOWN_THREE;
}

inline AnimationConfig animationConfigFor(Step step) {
    AnimationConfig config;
    config.loop = false;
    config.speed = 16;
    config.loopDelayMs = 0;

    switch (step) {
        case Step::THREE:
            config.initialState = threeLedState();
            break;
        case Step::TWO:
            config.initialState = twoLedState();
            break;
        case Step::ONE:
            config.initialState = oneLedState();
            break;
        case Step::DRAW:
            config.initialState = drawLedState();
            break;
    }

    return config;
}

struct Stage {
    Step step;
    unsigned long durationMs;
    AnimationConfig animationConfig;
};

inline Stage stageFor(Step step) {
    return Stage{
        step,
        step == Step::DRAW ? 0UL : kStepDurationMs,
        animationConfigFor(step),
    };
}

inline const Stage* queue() {
    static const Stage kQueue[] = {
        stageFor(Step::THREE),
        stageFor(Step::TWO),
        stageFor(Step::ONE),
        stageFor(Step::DRAW),
    };
    return kQueue;
}

inline constexpr size_t queueLength() {
    return 4;
}

inline const char* labelFor(Step step) {
    switch (step) {
        case Step::THREE:
            return "3";
        case Step::TWO:
            return "2";
        case Step::ONE:
            return "1";
        case Step::DRAW:
            return "DRAW";
    }
    return "";
}

inline unsigned long opponentResponseMsForMatch(int matchIndex) {
    static const unsigned long kSequence[] = {
        2000, 1000, 600, 400, 300, 250, 200, 175, 150, 150, 150, 125, 100, 100, 100, 100, 100, 50,
    };

    if (matchIndex < 0) {
        matchIndex = 0;
    }

    const size_t idx = static_cast<size_t>(matchIndex);
    if (idx >= sizeof(kSequence) / sizeof(kSequence[0])) {
        return 50;
    }

    return kSequence[idx];
}

enum class PeripheralAnimationId : uint8_t {
    IDLE = 0,
    VERTICAL_CHASE = 1,
    TRANSMIT_BREATH = 2,
    HUNTER_WIN = 3,
    BOUNTY_WIN = 4,
    COUNTDOWN = 5,
    LOSE = 6,
};

}  // namespace QuickdrawCountdown
