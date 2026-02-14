#pragma once

#include "animation-base.hpp"
#include "utils/simple-timer.hpp"
#include <algorithm> // For std::min

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
class TransmitBreathAnimation : public AnimationBase {
public:
    TransmitBreathAnimation() : 
        AnimationBase(),
        breathProgress_(0),
        breathingUp_(true) {
    }

protected:
    void onInit() override {
        // Reset animation state
        breathProgress_ = 0;
        breathingUp_ = true;
        
        // Set default frame rate for smooth breathing (20ms per frame)
        // For a complete breath cycle (0->255->0) at 2 units per frame:
        // 255 steps ÷ 2 = 127.5 frames × 20ms = ~2.55 seconds per cycle
        if (config_.speed == 0) {
            frameTimer_.setTimer(20);
        }
        
        // Initialize color from config
        if (config_.initialState.transmitLight.color.red == 0 &&
            config_.initialState.transmitLight.color.green == 0 &&
            config_.initialState.transmitLight.color.blue == 0) {
            // Default to white if no color specified
            ledColor_ = LEDColor(255, 255, 255);
        } else {
            // Use the color from the initial state
            ledColor_ = config_.initialState.transmitLight.color;
        }
        
        // Start with clear state
        currentState_ = config_.initialState;
        currentState_.clear();
    }

    LEDState onAnimate() override {
        // Update breathing progress based on direction
        if (breathingUp_) {
            breathProgress_ += 2; // Adjust for desired speed
            if (breathProgress_ >= 255) {
                breathProgress_ = 255;
                breathingUp_ = false;
            }
        } else {
            breathProgress_ -= 2; // Adjust for desired speed
            if (breathProgress_ <= 0) {
                breathProgress_ = 0;
                breathingUp_ = true;
                
                // If not looping, mark as complete after one full cycle
                if (!config_.loop) {
                    isComplete_ = true;
                }
            }
        }
        
        // Get smooth breathing value from ease-in-out curve for a more natural breathing effect
        uint8_t brightness = getEasingValue(breathProgress_, EaseCurve::EASE_IN_OUT);
        
        // Set only the transmit light with the calculated brightness
        currentState_.transmitLight = LEDState::SingleLEDState(ledColor_, brightness);
        
        return currentState_;
    }

private:
    uint8_t breathProgress_;    // Progress of breathing animation (0-255)
    bool breathingUp_;          // Whether we're breathing up (true) or down (false)
    LEDColor ledColor_;         // Color of the transmit LED
};