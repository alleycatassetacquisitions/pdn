#pragma once

#include "animation.hpp"
#include "simple-timer.hpp"

class CountdownAnimation : public Animation {
public:
    CountdownAnimation() : 
        Animation(),
        currentLed(3),           // Start with the first display LED (index 3)
        stopIndex(9),
        fadeProgress(0),         // Start with 0 brightness
        isWaitingBetweenLeds(false) {
        ledDelayTimer.setTimer(0);  // Initialize with 0ms timer (expired)
    }

protected:
    void onInit() override {
        // Reset animation state
        fadeProgress = 0;         // Start with 0 brightness
        isWaitingBetweenLeds = false;
        ledDelayTimer.setTimer(0);
        
        // Initialize with the provided initial state if needed
        // For countdown, we typically just use white LEDs, but we could
        // extract colors from initialState if desired

        currentState = config.initialState;

        for(int i = 3; i <= 9; i++) {
            if(i == 9 && currentState.leftLights[i-1].brightness == 255) {
                currentLed = i;
            } else if(currentState.leftLights[i].brightness == 255) {
                currentLed = i;
            } else if(currentState.leftLights[i].brightness == 0
                        && currentState.leftLights[i-1].brightness == 255) {
                currentLed = i;
            }
        }

        if(currentLed < 9) {
            stopIndex = currentLed + 2;
        }
    }

    LEDState onAnimate() override {
        // Clear the current state before updating
        currentState.clear();

        // First, set all LED's between 3 and currentLed_ to full brightness
        for(int i = 3; i < currentLed; i++) {
            currentState.setLEDPair(i, LEDColor(255, 255, 255), 255);
        }

        if (currentLed >= stopIndex) {
            
            // Mark animation as complete
            isComplete = true;
            
            return currentState;
        }

        // If we're in the delay period between LEDs
        if (isWaitingBetweenLeds) {
            // Keep previous LEDs at full brightness
            
            
            if (ledDelayTimer.expired()) {
                isWaitingBetweenLeds = false;
                currentLed++;           // Move to next LED
                fadeProgress = 0;       // Reset fade progress for new LED
            }
            
            return currentState;
        }

        // Get smooth fade value from ease-in-out curve
        uint8_t brightness = getEasingValue(fadeProgress, EaseCurve::EASE_IN_OUT);

        // Set current LED brightness
        currentState.setLEDPair(currentLed, LEDColor(255, 255, 255), brightness);

        // Update fade progress
        fadeProgress += 5;  // Adjust this value to control fade speed

        // When fade is complete, start delay before next LED
        if (fadeProgress >= 250) {
            isWaitingBetweenLeds = true;
            ledDelayTimer.setTimer(config.loopDelayMs);  // Default 200ms delay between LEDs
        }

        return currentState;
    }

private:
    uint8_t currentLed;           // Current LED being animated (3-8)
    uint8_t stopIndex;              // LED to stop on
    uint8_t fadeProgress;         // Progress of current LED's fade (0-255)
    bool isWaitingBetweenLeds;    // Whether we're in the delay period between LEDs
    SimpleTimer ledDelayTimer;    // Timer for delay between LEDs
}; 