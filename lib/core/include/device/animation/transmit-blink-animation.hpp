#pragma once

#include "device/animation/animation-base.hpp"
#include "utils/simple-timer.hpp"

/** Blinks only the transmit light in one color, on for @p onMs then off for @p offMs, until stopped. */
class TransmitBlinkAnimation : public AnimationBase {
public:
    /** Lit at @p brightness for @p onMs, then dark for @p offMs, repeating. */
    TransmitBlinkAnimation(LEDColor color, uint8_t brightness, unsigned long onMs, unsigned long offMs)
        : color(color)
        , brightness(brightness)
        , onMs(onMs)
        , offMs(offMs) {}

protected:
    void onInit() override {
        startMs = SimpleTimer::getPlatformClock()->milliseconds();
        currentState_ = lit();
    }

    LEDState onAnimate() override {
        const unsigned long elapsed = SimpleTimer::getPlatformClock()->milliseconds() - startMs;
        return elapsed % (onMs + offMs) < onMs ? lit() : LEDState();
    }

private:
    LEDState lit() const {
        LEDState state;
        state.transmitLight = LEDState::SingleLEDState(color, brightness);
        return state;
    }

    LEDColor color;
    uint8_t brightness;
    unsigned long onMs;
    unsigned long offMs;
    unsigned long startMs = 0;
};
