#pragma once

#include "device/animation/animation-base.hpp"
#include <random>

// Celebration animation: all LEDs flash white with a twinkle fade.
class FDNMatchSuccessAnimation : public AnimationBase {
public:
    FDNMatchSuccessAnimation() = default;

protected:
    void onInit() override {
        currentState_ = config_.initialState;
        currentState_.clear();
    }

    LEDState onAnimate() override {
        // Decay all lights
        for (uint8_t i = 0; i < 9; ++i) {
            currentState_.leftLights[i].brightness
                = static_cast<uint8_t>(currentState_.leftLights[i].brightness * 92 / 100);
            currentState_.rightLights[i].brightness
                = static_cast<uint8_t>(currentState_.rightLights[i].brightness * 92 / 100);
        }

        // Random twinkles
        static constexpr LEDColor kWhite(255, 255, 255);
        static constexpr LEDColor kGold(255, 180, 0);
        if (rng() % 3 == 0) {
            uint8_t idx = static_cast<uint8_t>(rng() % 9);
            bool isLeft = (rng() % 2 == 0);
            LEDColor color = (rng() % 2 == 0) ? kWhite : kGold;
            if (isLeft) {
                currentState_.leftLights[idx] = LEDState::SingleLEDState(color, 255);
            } else {
                currentState_.rightLights[idx] = LEDState::SingleLEDState(color, 255);
            }
        }
        return currentState_;
    }

private:
    std::minstd_rand rng{42};
};
