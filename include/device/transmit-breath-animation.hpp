#pragma once

#include "animation.hpp"
#include "simple-timer.hpp"
#include <algorithm> // For std::min
#include "esp_log.h"

/*
 * TransmitBreathAnimation
 * 
 * An animation that makes the transmit LED "breathe" by fading in and out.
 * 
 * Usage example:
 * 
 * ```cpp
 * AnimationConfig config;
 * config.type = AnimationType::TRANSMIT_BREATH;
 * config.speed = 20;                         // 20ms per frame (optional)
 * config.curve = EaseCurve::EASE_IN_OUT;     // Smooth breathing curve (optional)
 * config.loop = true;                        // Repeat indefinitely
 * 
 * // Set custom LED color (optional - defaults to white)
 * LEDState initialState;
 * initialState.transmitLight = LEDState::SingleLEDState(LEDColor(0, 0, 255), 0); // Blue color
 * config.initialState = initialState;
 * 
 * device->startAnimation(config);
 * ```
 */
class TransmitBreathAnimation : public Animation {
public:
    TransmitBreathAnimation() : 
        Animation(),
        breathProgress(0),
        breathingUp(true) {
    }

protected:
    void onInit() override {
        // Reset animation state
        breathProgress = 0;
        breathingUp = true;
        
        // Set default frame rate for smooth breathing (20ms per frame)
        // For a complete breath cycle (0->255->0) at 2 units per frame:
        // 255 steps ÷ 2 = 127.5 frames × 20ms = ~2.55 seconds per cycle
        if (config.speed == 0) {
            frameTimer.setTimer(20);
        }
        
        // Initialize color from config
        if (config.initialState.transmitLight.color.red == 0 &&
            config.initialState.transmitLight.color.green == 0 &&
            config.initialState.transmitLight.color.blue == 0) {
            // Default to white if no color specified
            ledColor = LEDColor(255, 255, 255);
        } else {
            // Use the color from the initial state
            ledColor = config.initialState.transmitLight.color;
        }
        
        // Start with clear state
        currentState = config.initialState;
        currentState.clear();
    }

    LEDState onAnimate() override {
        // Update breathing progress based on direction
        if (breathingUp) {
            breathProgress += 2; // Adjust for desired speed
            if (breathProgress >= 255) {
                breathProgress = 255;
                breathingUp = false;
            }
        } else {
            breathProgress -= 2; // Adjust for desired speed
            if (breathProgress <= 0) {
                breathProgress = 0;
                breathingUp = true;
                
                // If not looping, mark as complete after one full cycle
                if (!config.loop) {
                    isComplete = true;
                }
            }
        }
        
        // Get smooth breathing value from ease-in-out curve for a more natural breathing effect
        uint8_t brightness = getEasingValue(breathProgress, EaseCurve::EASE_IN_OUT);
        
        // Set only the transmit light with the calculated brightness
        currentState.transmitLight = LEDState::SingleLEDState(ledColor, brightness);
        
        return currentState;
    }

private:
    uint8_t breathProgress;    // Progress of breathing animation (0-255)
    bool breathingUp;          // Whether we're breathing up (true) or down (false)
    LEDColor ledColor;         // Color of the transmit LED
}; 