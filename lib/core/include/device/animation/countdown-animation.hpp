#pragma once

#include "device/animation/animation-base.hpp"
#include "utils/simple-timer.hpp"

class CountdownAnimation : public AnimationBase {
public:
    CountdownAnimation()
        : AnimationBase()
        , currentLed_(3)
        , stopIndex_(9)
        , fadeProgress_(0)
        , isWaitingBetweenLeds_(false) {
        ledDelayTimer_.setTimer(0);
    }

protected:
    void onInit() override {
        fadeProgress_ = 0;
        isWaitingBetweenLeds_ = false;
        ledDelayTimer_.setTimer(0);
        currentState_ = config_.initialState;

        for (int i = 3; i <= 9; i++) {
            if (i == 9 && currentState_.leftLights[i - 1].brightness == 255) {
                currentLed_ = i;
            } else if (i < 9 && currentState_.leftLights[i].brightness == 255) {
                currentLed_ = i;
            } else if (i < 9 && currentState_.leftLights[i].brightness == 0
                       && currentState_.leftLights[i - 1].brightness == 255) {
                currentLed_ = i;
            }
        }

        if (currentLed_ < 9) {
            stopIndex_ = currentLed_ + 2;
        }
    }

    LEDState onAnimate() override {
        currentState_.clear();

        for (int i = 3; i < currentLed_; i++) {
            currentState_.setLEDPair(i, LEDColor(255, 255, 255), 255);
        }

        if (currentLed_ >= stopIndex_) {
            isComplete_ = true;
            return currentState_;
        }

        if (isWaitingBetweenLeds_) {
            if (ledDelayTimer_.expired()) {
                isWaitingBetweenLeds_ = false;
                currentLed_++;
                fadeProgress_ = 0;
            }
            return currentState_;
        }

        uint8_t brightness = getEasingValue(fadeProgress_, EaseCurve::EASE_IN_OUT);
        currentState_.setLEDPair(currentLed_, LEDColor(255, 255, 255), brightness);
        fadeProgress_ += 5;

        if (fadeProgress_ >= 250) {
            isWaitingBetweenLeds_ = true;
            ledDelayTimer_.setTimer(config_.loopDelayMs);
        }

        return currentState_;
    }

private:
    uint8_t currentLed_;
    uint8_t stopIndex_;
    uint8_t fadeProgress_;
    bool isWaitingBetweenLeds_;
    SimpleTimer ledDelayTimer_;
};
