#pragma once

#include "animation-base.hpp"
#include "simple-timer.hpp"
#include <algorithm> // For std::min
#include "esp_log.h"

class BountyWinAnimation : public AnimationBase {
public:
    BountyWinAnimation() : 
        AnimationBase(),
        lastFrameTime_(0) {
        ESP_LOGI("BountyWinAnimation", "Constructor called");
    }

protected:
    void onInit() override {
        ESP_LOGI("BountyWinAnimation", "onInit called");
        // Reset animation state
        lastFrameTime_ = 0;
        
        // Set default frame rate for smooth animation (16ms per frame)
        if (config_.speed == 0) {
            frameTimer_.setTimer(16);
        }
        
        // Initialize with clear state
        currentState_ = config_.initialState;
        currentState_.clear();
        currentState_.leftLights[0].color = gripColors[0];
        currentState_.leftLights[1].color = gripColors[1];
        currentState_.leftLights[2].color = gripColors[2];
        currentState_.rightLights[0].color = gripColors[3];
        currentState_.rightLights[1].color = gripColors[4];
        currentState_.rightLights[2].color = gripColors[5];
        ESP_LOGI("BountyWinAnimation", "Initialized with frame timer: %d ms", config_.speed);
    }

    LEDState onAnimate() override {
        uint32_t currentTime = millis();
        
        // Decay all LED brightnesses by 5%
        for (int i = 0; i < 9; i++) {
            LEDState::SingleLEDState& leftState = currentState_.leftLights[i];
            LEDState::SingleLEDState& rightState = currentState_.rightLights[i];
            
            // Decay brightness
            leftState.brightness = (leftState.brightness * 95) / 100;
            rightState.brightness = (rightState.brightness * 95) / 100;
        }
        
        // Decay transmit light
        currentState_.transmitLight.brightness = (currentState_.transmitLight.brightness * 95) / 100;
        
        if (random(3) == 0) {
            currentState_.setLED(random(2) == 0, random(6)+3, twinkleColor, 255);

            int index = random(6);
            if (index < 3) {
                currentState_.setLED(true, index, gripColors[index], 255);
            } else {
                currentState_.setLED(false, index-3, gripColors[index], 255);
            }
            
            ESP_LOGI("BountyWinAnimation", "Added six twinkles to display and grip LEDs");
        }
        
        return currentState_;
    }

private:
    uint32_t lastFrameTime_;   // Last frame time for timing calculations
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