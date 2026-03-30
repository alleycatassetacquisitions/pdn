#pragma once

#include "animation-base.hpp"
#include "utils/simple-timer.hpp"
#include "apps/main-menu/main-menu-resources.hpp"
#include <algorithm>

class MainMenuAnimation : public AnimationBase {
public:
    MainMenuAnimation() :
        AnimationBase(),
        currentIndex_(NUM_STEPS),
        progress_(0),
        isWaitingForPause_(false) {
        pauseTimer_.setTimer(0);
    }

protected:
    void onInit() override {
        currentIndex_ = NUM_STEPS;
        progress_ = 0;
        isWaitingForPause_ = false;
        pauseTimer_.setTimer(0);
        currentState_ = config_.initialState;
    }

    LEDState onAnimate() override {
        if (isWaitingForPause_) {
            if (pauseTimer_.expired()) {
                isWaitingForPause_ = false;
                currentIndex_ = NUM_STEPS;
                progress_ = 0;
            } else {
                return currentState_;
            }
        }

        progress_ = std::min(progress_ + 20, 255);
        int bezierValue = getEasingValue(progress_, config_.curve);

        currentState_.clear();

        for (int i = 0; i < NUM_FIN_LIGHTS; i++) {
            int colorIdx = i - currentIndex_ + 4;
            if (colorIdx < 0 || colorIdx > 3) continue;

            uint8_t brightness;
            if (colorIdx == 0) {
                brightness = static_cast<uint8_t>(std::min(bezierValue, 75));
            } else if (colorIdx == 3) {
                brightness = static_cast<uint8_t>(std::min(255 - bezierValue, 35));
            } else {
                brightness = 35;
            }

            currentState_.setFinLight(i, mainMenuColors[colorIdx], brightness);
        }

        if (progress_ == 255) {
            progress_ = 0;
            currentIndex_--;

            if (currentIndex_ <= 0) {
                if (config_.loop) {
                    isWaitingForPause_ = true;
                    pauseTimer_.setTimer(config_.loopDelayMs);
                } else {
                    isComplete_ = true;
                }
            }
        }

        return currentState_;
    }

private:
    static constexpr uint8_t NUM_FIN_LIGHTS = 9;
    static constexpr uint8_t NUM_STEPS = NUM_FIN_LIGHTS + 3;

    uint8_t currentIndex_;
    uint8_t progress_;
    bool isWaitingForPause_;
    SimpleTimer pauseTimer_;
};
