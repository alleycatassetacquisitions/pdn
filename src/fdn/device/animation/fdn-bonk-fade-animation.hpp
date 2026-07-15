#pragma once

#include "device/animation/animation-base.hpp"

// All FDN fin + recess LEDs breathe in unison (orange).
class FdnBonkFadeAnimation : public AnimationBase {
public:
    FdnBonkFadeAnimation() : AnimationBase(), step_(0), goingUp_(true), brightness_(0) {}

protected:
    void onInit() override {
        step_         = 0;
        goingUp_      = true;
        brightness_   = 0;
        currentState_ = config_.initialState;
        currentState_.clear();
    }

    LEDState onAnimate() override {
        static constexpr uint8_t kMinBrightness = 20;
        static constexpr uint8_t kMaxBrightness = 200;
        static constexpr int     kSteps         = 40;

        if (goingUp_) {
            brightness_ = static_cast<uint8_t>(
                kMinBrightness + (kMaxBrightness - kMinBrightness) * step_ / kSteps);
            if (++step_ > kSteps) {
                step_    = kSteps;
                goingUp_ = false;
            }
        } else {
            brightness_ = static_cast<uint8_t>(
                kMinBrightness + (kMaxBrightness - kMinBrightness) * step_ / kSteps);
            if (step_-- == 0) {
                step_    = 0;
                goingUp_ = true;
            }
        }

        currentState_.clear();
        static constexpr LEDColor kOrange(255, 128, 0);
        for (uint8_t i = 0; i < 9; ++i) {
            currentState_.leftLights[i]  = LEDState::SingleLEDState(kOrange, brightness_);
            currentState_.rightLights[i] = LEDState::SingleLEDState(kOrange, brightness_);
        }
        return currentState_;
    }

private:
    int     step_;
    bool    goingUp_;
    uint8_t brightness_;
};
