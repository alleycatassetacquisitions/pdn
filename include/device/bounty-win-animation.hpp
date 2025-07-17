#pragma once

#include "animation-base.hpp"
#include "simple-timer.hpp"
#include <algorithm> // For std::min
#include "esp_log.h"

class BountyWinAnimation : public AnimationBase {
public:
    BountyWinAnimation() : 
        AnimationBase() {
    }

protected:
    void onInit() override {
        
        // Set default frame rate for smooth animation (16ms per frame)
        if (config.speed == 0) {
            frameTimer.setTimer(16);
        }
        
        // Initialize with clear state
        currentState = config.initialState;
        currentState.clear();
        currentState.leftLights[0].color = gripColors[0];
        currentState.leftLights[1].color = gripColors[1];
        currentState.leftLights[2].color = gripColors[2];
        currentState.rightLights[0].color = gripColors[3];
        currentState.rightLights[1].color = gripColors[4];
        currentState.rightLights[2].color = gripColors[5];
    }

    LEDState onAnimate() override {
        uint32_t currentTime = millis();
        
        // Decay all LED brightnesses by 5%
        for (int i = 0; i < 9; i++) {
            LEDState::SingleLEDState& leftState = currentState.leftLights[i];
            LEDState::SingleLEDState& rightState = currentState.rightLights[i];
            
            // Decay brightness
            leftState.brightness = (leftState.brightness * 95) / 100;
            rightState.brightness = (rightState.brightness * 95) / 100;
        }
        
        // Decay transmit light
        currentState.transmitLight.brightness = (currentState.transmitLight.brightness * 95) / 100;
        
        if (random(3) == 0) {
            currentState.setLED(random(2) == 0, random(6)+3, twinkleColor, 255);

            int index = random(6);
            if (index < 3) {
                currentState.setLED(true, index, gripColors[index], 255);
            } else {
                currentState.setLED(false, index-3, gripColors[index], 255);
            }
            
        }
        
        return currentState;
    }

private:
    LEDColor twinkleColor = LEDColor(222, 97, 7);
    LEDColor gripColors[6] = {
        bountyIdleLEDColors[0],
        bountyIdleLEDColors[1],
        bountyIdleLEDColors[2],
        bountyIdleLEDColors[3],
        bountyIdleLEDColors[4],
        bountyIdleLEDColors[5]
    };
}; 