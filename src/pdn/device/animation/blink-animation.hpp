#pragma once

#include "device/animation/animation-base.hpp"
#include "utils/simple-timer.hpp"

/** Blinks every LED pair in one color @p blinks times (on, off, on, ...), then stays off. */
class BlinkAnimation : public AnimationBase {
public:
    BlinkAnimation(LEDColor color, uint8_t brightness, unsigned long onMs, unsigned long offMs,
                   uint8_t blinks)
        : AnimationBase()
        , color_(color)
        , brightness_(brightness)
        , onMs_(onMs)
        , offMs_(offMs)
        , blinks_(blinks) {}

protected:
    void onInit() override {
        startMs_ = SimpleTimer::getPlatformClock()->milliseconds();
        currentState_ = lit();
    }

    LEDState onAnimate() override {
        const unsigned long elapsed = SimpleTimer::getPlatformClock()->milliseconds() - startMs_;
        const unsigned long period = onMs_ + offMs_;
        if (blinks_ == 0 || elapsed >= period * (blinks_ - 1) + onMs_) {
            isComplete_ = true;
            return LEDState();
        }
        return elapsed % period < onMs_ ? lit() : LEDState();
    }

private:
    LEDState lit() const {
        LEDState state;
        for (uint8_t pair = 0; pair < 9; pair++) {
            state.setLEDPair(pair, color_, brightness_);
        }
        return state;
    }

    LEDColor color_;
    uint8_t brightness_;
    unsigned long onMs_;
    unsigned long offMs_;
    uint8_t blinks_;
    unsigned long startMs_ = 0;
};
