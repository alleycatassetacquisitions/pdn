#pragma once

#include "device/animation/animation-base.hpp"
#include "utils/simple-timer.hpp"

/**
 * Lights the display LED pairs (3-8) with one color per pair, then drains them to black over
 * a fixed duration, last pair first. The pair currently draining fades instead of snapping off.
 */
class StepDrainAnimation : public AnimationBase {
public:
    static constexpr uint8_t FIRST_PAIR = 3;
    static constexpr uint8_t PAIR_COUNT = 6;

    StepDrainAnimation(const LEDColor (&colors)[PAIR_COUNT], uint8_t brightness,
                       unsigned long durationMs)
        : AnimationBase()
        , brightness_(brightness)
        , durationMs_(durationMs > 0 ? durationMs : 1) {
        for (uint8_t pair = 0; pair < PAIR_COUNT; pair++) {
            colors_[pair] = colors[pair];
        }
    }

protected:
    void onInit() override {
        startMs_ = SimpleTimer::getPlatformClock()->milliseconds();
        currentState_ = drawLevel(PAIR_COUNT * 255UL);
    }

    LEDState onAnimate() override {
        const unsigned long elapsed = SimpleTimer::getPlatformClock()->milliseconds() - startMs_;
        if (elapsed >= durationMs_) {
            isComplete_ = true;
            return LEDState();
        }
        return drawLevel(PAIR_COUNT * 255UL * (durationMs_ - elapsed) / durationMs_);
    }

private:
    /** @p level is lit pairs × 255: whole pairs are full, the remainder dims the next pair. */
    LEDState drawLevel(unsigned long level) const {
        LEDState state;
        for (uint8_t pair = 0; pair < PAIR_COUNT && level > 0; pair++) {
            const unsigned long fill = level >= 255 ? 255 : level;
            state.setLEDPair(FIRST_PAIR + pair, colors_[pair],
                             static_cast<uint8_t>(brightness_ * fill / 255));
            level -= fill;
        }
        return state;
    }

    LEDColor colors_[PAIR_COUNT];
    uint8_t brightness_;
    unsigned long durationMs_;
    unsigned long startMs_ = 0;
};
