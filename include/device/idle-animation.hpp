#pragma once

#include "animation.hpp"
#include "simple-timer.hpp"
#include <algorithm> // For std::min
#include "esp_log.h"

class IdleAnimation : public Animation {
public:
    IdleAnimation() : 
        Animation(),
        currentOffset(0),
        transitionProgress(0),
        isWaitingForPause(false) {
        pauseTimer.setTimer(0);
    }

protected:
    void onInit() override {
        currentOffset = 0;
        transitionProgress = 0;
        isWaitingForPause = false;
        
        // Store initial colors
        for (int i = 0; i < 9; i++) {
            originalColors[i] = config.initialState.leftLights[i].color;
        }
        
        pauseTimer.setTimer(0);
        currentState = config.initialState;
    }

    LEDState onAnimate() override {
        if (isWaitingForPause) {
            if (pauseTimer.expired()) {
                isWaitingForPause = false;
            } else {
                return currentState;
            }
        }
        
        // Calculate interpolation factor (0.0 to 1.0)
        const int TRANSITION_STEPS = 30;
        float interpolationFactor = (float)transitionProgress / TRANSITION_STEPS;
        
        // For each LED position
        for (int i = 0; i < 9; i++) {
            // Calculate current and next color indices
            int currentColorIndex = (i + currentOffset) % 9;
            int nextColorIndex = (i + currentOffset + 1) % 9;
            
            // Get current and next colors
            LEDColor currentColor = originalColors[currentColorIndex];
            LEDColor nextColor = originalColors[nextColorIndex];
            
            // Interpolate between colors
            uint8_t r = currentColor.red + (uint8_t)(interpolationFactor * (nextColor.red - currentColor.red));
            uint8_t g = currentColor.green + (uint8_t)(interpolationFactor * (nextColor.green - currentColor.green));
            uint8_t b = currentColor.blue + (uint8_t)(interpolationFactor * (nextColor.blue - currentColor.blue));
            
            // Set the interpolated color
            LEDColor interpolatedColor = {r, g, b};
            if(i > 2) {
                currentState.setLEDPair(i, interpolatedColor, (int)(brightness * brightnessRatio));
            } else {
                currentState.setLEDPair(i, interpolatedColor, brightness);
            }
        }
        
        // Increment transition progress
        transitionProgress++;
        
        // When transition is complete, move to next offset
        if (transitionProgress >= TRANSITION_STEPS) {
            transitionProgress = 0;
            currentOffset = (currentOffset + 1) % 9;
            
            // If we've completed a full cycle and looping is enabled, add a pause
            if (currentOffset == 0 && config.loop) {
                isWaitingForPause = true;
                pauseTimer.setTimer(config.loopDelayMs);
            }
        }
        
        return currentState;
    }

private:
    LEDColor originalColors[9];
    int currentOffset;
    int transitionProgress;
    bool isWaitingForPause;
    SimpleTimer pauseTimer;
    uint8_t brightness = 87;
    float brightnessRatio = .2;
}; 