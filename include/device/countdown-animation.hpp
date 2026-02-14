#pragma once

#include "animation-base.hpp"
#include "utils/simple-timer.hpp"

class CountdownAnimation : public AnimationBase {
public:
    CountdownAnimation() : 
        AnimationBase(),
        currentLed_(3),           // Start with the first display LED (index 3)
        stopIndex_(9),
        fadeProgress_(0),         // Start with 0 brightness
        isWaitingBetweenLeds_(false) {
        ledDelayTimer_.setTimer(0);  // Initialize with 0ms timer (expired)
    }

protected:
    void onInit() override {
        // Reset animation state
        fadeProgress_ = 0;         // Start with 0 brightness
        isWaitingBetweenLeds_ = false;
        ledDelayTimer_.setTimer(0);
        
        // Initialize with the provided initial state if needed
        // For countdown, we typically just use white LEDs, but we could
        // extract colors from initialState if desired

        currentState_ = config_.initialState;

        for(int i = 3; i <= 9; i++) {
            if(i == 9 && currentState_.leftLights[i-1].brightness == 255) {
                currentLed_ = i;
            } else if(currentState_.leftLights[i].brightness == 255) {
                currentLed_ = i;
            } else if(currentState_.leftLights[i].brightness == 0
                        && currentState_.leftLights[i-1].brightness == 255) {
                currentLed_ = i;
            }
        }

        if(currentLed_ < 9) {
            stopIndex_ = currentLed_ + 2;
        }
    }

    LEDState onAnimate() override {
        // Clear the current state before updating
        currentState_.clear();

        // First, set all LED's between 3 and currentLed_ to full brightness
        for(int i = 3; i < currentLed_; i++) {
            currentState_.setLEDPair(i, LEDColor(255, 255, 255), 255);
        }

        if (currentLed_ >= stopIndex_) {
            
            // Mark animation as complete
            isComplete_ = true;
            
            return currentState_;
        }

        // If we're in the delay period between LEDs
        if (isWaitingBetweenLeds_) {
            // Keep previous LEDs at full brightness
            
            
            if (ledDelayTimer_.expired()) {
                isWaitingBetweenLeds_ = false;
                currentLed_++;           // Move to next LED
                fadeProgress_ = 0;       // Reset fade progress for new LED
            }
            
            return currentState_;
        }

        // Get smooth fade value from ease-in-out curve
        uint8_t brightness = getEasingValue(fadeProgress_, EaseCurve::EASE_IN_OUT);

        // Set current LED brightness
        currentState_.setLEDPair(currentLed_, LEDColor(255, 255, 255), brightness);

        // Update fade progress
        fadeProgress_ += 5;  // Adjust this value to control fade speed

        // When fade is complete, start delay before next LED
        if (fadeProgress_ >= 250) {
            isWaitingBetweenLeds_ = true;
            ledDelayTimer_.setTimer(config_.loopDelayMs);  // Default 200ms delay between LEDs
        }

        return currentState_;
    }

private:
    uint8_t currentLed_;           // Current LED being animated (3-8)
    uint8_t stopIndex_;              // LED to stop on
    uint8_t fadeProgress_;         // Progress of current LED's fade (0-255)
    bool isWaitingBetweenLeds_;    // Whether we're in the delay period between LEDs
    SimpleTimer ledDelayTimer_;    // Timer for delay between LEDs
};