#pragma once

#include "device/animation/animation-base.hpp"
#include "utils/simple-timer.hpp"
#include <random>

// BountyWinAnimation (PDN) counterpart for the FDN.
class FDNBountyWinAnimation : public AnimationBase {
public:
    FDNBountyWinAnimation() : AnimationBase(), lastFrameTime_(0) {}

protected:
    void onInit() override {
        lastFrameTime_ = 0;
        if (config_.speed == 0) {
            frameTimer_.setTimer(16);
        }
        currentState_ = config_.initialState;
        currentState_.clear();
        currentState_.leftLights[0].color  = gripColors[0];
        currentState_.leftLights[1].color  = gripColors[1];
        currentState_.leftLights[2].color  = gripColors[2];
        currentState_.rightLights[0].color = gripColors[3];
        currentState_.rightLights[1].color = gripColors[4];
        currentState_.rightLights[2].color = gripColors[5];
    }

    LEDState onAnimate() override {
        for (int i = 0; i < 9; i++) {
            currentState_.leftLights[i].brightness  = (currentState_.leftLights[i].brightness  * 95) / 100;
            currentState_.rightLights[i].brightness = (currentState_.rightLights[i].brightness * 95) / 100;
        }
        currentState_.transmitLight.brightness = (currentState_.transmitLight.brightness * 95) / 100;

        if (randomInt(0, 2) == 0) {
            currentState_.setLED(randomInt(0, 1) == 0, randomInt(0, 5) + 3, twinkleColor, 255);

            int index = randomInt(0, 5);
            if (index < 3) {
                currentState_.setLED(true,  index,     gripColors[index], 255);
            } else {
                currentState_.setLED(false, index - 3, gripColors[index], 155);
            }
        }

        return currentState_;
    }

private:
    static int randomInt(int min, int max) {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        std::uniform_int_distribution<> dist(min, max);
        return dist(gen);
    }

    uint32_t lastFrameTime_;

    // bountyIdleLEDColors[0-5] from quickdraw-resources.hpp
    LEDColor gripColors[6] = {
        LEDColor(255, 2, 1),
        LEDColor(237, 75, 0),
        LEDColor(255, 51, 0),
        LEDColor(222, 97, 7),
        LEDColor(255, 2, 1),
        LEDColor(237, 75, 0),
    };

    LEDColor twinkleColor = LEDColor(222, 97, 7);
};
