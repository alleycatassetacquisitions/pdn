#pragma once

#include "device/animation/animation-base.hpp"

// Pulsing animation triggered when a nearby player is detected.
// config_.initialState should have the brightness of each light pre-set to the
// desired peak intensity so different RSSI levels can be reflected.
class FDNPlayerDetectedAnimation : public AnimationBase {
public:
    FDNPlayerDetectedAnimation() = default;

protected:
    void onInit() override {
        step_      = 0;
        goingUp_   = true;
        currentState_ = config_.initialState;
    }

    LEDState onAnimate() override {
        const int STEPS = 40;
        float factor = static_cast<float>(step_) / STEPS;
        if (config_.curve == EaseCurve::EASE_IN_OUT) {
            factor = factor * factor * (3.0f - 2.0f * factor);
        }

        for (uint8_t i = 0; i < 9; ++i) {
            uint8_t peak = config_.initialState.leftLights[i].brightness;
            currentState_.leftLights[i].brightness  = static_cast<uint8_t>(peak * factor);
            currentState_.leftLights[i].color        = config_.initialState.leftLights[i].color;

            peak = config_.initialState.rightLights[i].brightness;
            currentState_.rightLights[i].brightness = static_cast<uint8_t>(peak * factor);
            currentState_.rightLights[i].color       = config_.initialState.rightLights[i].color;
        }

        if (goingUp_) {
            if (++step_ >= STEPS) { step_ = STEPS; goingUp_ = false; }
        } else {
            if (step_-- == 0) { step_ = 0; goingUp_ = true; }
        }

        return currentState_;
    }

private:
    int  step_    = 0;
    bool goingUp_ = true;
};
