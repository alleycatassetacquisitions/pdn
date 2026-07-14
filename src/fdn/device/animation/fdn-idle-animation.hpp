#pragma once

#include "device/animation/animation-base.hpp"

// Gentle breathing animation on FDN LEDs.
// leftLights[0-8]  → fin lights   (hunter green — one player side)
// rightLights[0-8] → first 9 recess lights (bounty orange — other player side)
class FDNIdleAnimation : public AnimationBase {
public:
    FDNIdleAnimation() = default;

protected:
    void onInit() override {
        step_         = 0;
        goingUp_      = true;
        brightness_   = 0;
        currentState_ = config_.initialState;
    }

    LEDState onAnimate() override {
        const uint8_t MIN_B = 20;
        const uint8_t MAX_B = 120;
        const int     STEPS = 60;

        if (goingUp_) {
            brightness_ = static_cast<uint8_t>(MIN_B + (MAX_B - MIN_B) * step_ / STEPS);
            if (++step_ > STEPS) { step_ = STEPS; goingUp_ = false; }
        } else {
            brightness_ = static_cast<uint8_t>(MIN_B + (MAX_B - MIN_B) * step_ / STEPS);
            if (step_-- == 0) { step_ = 0; goingUp_ = true; }
        }

        // Hunter green on fin lights, bounty orange on recess lights — matching PDN role palettes.
        static constexpr LEDColor kHunter(0, 200, 100);
        static constexpr LEDColor kBounty(237, 75, 0);
        for (uint8_t i = 0; i < 9; ++i) {
            currentState_.leftLights[i]  = LEDState::SingleLEDState(kHunter, brightness_);
            currentState_.rightLights[i] = LEDState::SingleLEDState(kBounty, brightness_ / 2);
        }
        return currentState_;
    }

private:
    int     step_       = 0;
    bool    goingUp_    = true;
    uint8_t brightness_ = 0;
};
