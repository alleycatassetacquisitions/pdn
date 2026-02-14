#pragma once

#include "animation-base.hpp"
#include "utils/simple-timer.hpp"
#include <algorithm> // For std::min

class IdleAnimation : public AnimationBase {
public:
    IdleAnimation() : 
        AnimationBase(),
        currentOffset(0),
        transitionProgress(0),
        isWaitingForPause_(false) {
        pauseTimer_.setTimer(0);
    }

protected:
    void onInit() override {
        currentOffset = 0;
        transitionProgress = 0;
        isWaitingForPause_ = false;
        
        // Store initial colors
        for (int i = 0; i < 9; i++) {
            originalColors[i] = config_.initialState.leftLights[i].color;
        }
        
        pauseTimer_.setTimer(0);
        currentState_ = config_.initialState;
    }

    LEDState onAnimate() override {
        if (isWaitingForPause_) {
            if (pauseTimer_.expired()) {
                isWaitingForPause_ = false;
            } else {
                return currentState_;
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
                currentState_.setLEDPair(i, interpolatedColor, (int)(brightness * brightnessRatio));
            } else {
                currentState_.setLEDPair(i, interpolatedColor, brightness);
            }
        }
        
        // Increment transition progress
        transitionProgress++;
        
        // When transition is complete, move to next offset
        if (transitionProgress >= TRANSITION_STEPS) {
            transitionProgress = 0;
            currentOffset = (currentOffset + 1) % 9;
            
            // If we've completed a full cycle and looping is enabled, add a pause
            if (currentOffset == 0 && config_.loop) {
                isWaitingForPause_ = true;
                pauseTimer_.setTimer(config_.loopDelayMs);
            }
        }
        
        return currentState_;
    }

private:
    LEDColor originalColors[9];
    int currentOffset;
    int transitionProgress;
    bool isWaitingForPause_;
    SimpleTimer pauseTimer_;
    uint8_t brightness = 87;
    float brightnessRatio = .2;
};