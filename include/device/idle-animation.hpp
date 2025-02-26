#pragma once

#include "animation-base.hpp"
#include "simple-timer.hpp"

class IdleAnimation : public AnimationBase {
public:
    IdleAnimation() : 
        AnimationBase(),
        currentIndex_(8),  // Start at top
        prevIndex_(8),    // Start at top
        brightness_(0),
        isFadingOut_(false),
        isWaitingForPause_(false) {
        // Initialize with 0ms timer (expired)
        pauseTimer_.setTimer(0);
    }

protected:
    void onInit() override {
        currentIndex_ = 8;  // Start at top
        prevIndex_ = 8;     // Start at top
        brightness_ = 0;
        isFadingOut_ = false;
        isWaitingForPause_ = false;
        pauseTimer_.setTimer(0);  // Reset pause timer
        clearState(currentState_);
    }

    LEDState onAnimate(uint32_t currentTime) override {
        LEDState newState;
        clearState(newState);

        // If we're in the pause state
        if (isWaitingForPause_) {
            if (pauseTimer_.expired()) {
                isWaitingForPause_ = false;
                currentIndex_ = 8;  // Reset to top
                prevIndex_ = 8;     // Reset to top
                brightness_ = 0;
            }
            return newState;  // Return empty state during pause
        }

        if (isFadingOut_) {
            // Only show the bottom LED fading out
            if (brightness_ >= 45) {
                brightness_ -= 45;
            } else {
                brightness_ = 0;
            }

            setLEDPair(newState, 0, LEDColor(255, 255, 255), brightness_);  // Bottom LED (0)

            if (brightness_ == 0) {
                isFadingOut_ = false;
                isWaitingForPause_ = true;
                pauseTimer_.setTimer(250);  // 250ms pause
            }
            return newState;
        }

        // Normal animation sequence
        // Set the current LED pair to white with increasing brightness
        setLEDPair(newState, currentIndex_, LEDColor(255, 255, 255), brightness_);

        // Set the previous LED pair with decreasing brightness
        if (prevIndex_ != currentIndex_) {
            uint8_t prevBrightness = 255 - brightness_;  // Inverse fade
            setLEDPair(newState, prevIndex_, LEDColor(255, 255, 255), prevBrightness);
        }

        // Update brightness
        static const uint8_t brightnessStep = 45;
        if (brightness_ <= (255 - brightnessStep)) {
            brightness_ += brightnessStep;
        } else {
            brightness_ = 255;
        }

        // When we reach full brightness, move to next position
        if (brightness_ >= 255) {
            if (currentIndex_ == 0) {  // If we're at the bottom
                isFadingOut_ = true;   // Start the fade out sequence
            } else {
                brightness_ = 0;  // Reset brightness for next position
                prevIndex_ = currentIndex_;  // Store current position as previous
                currentIndex_--;  // Move to next position (moving downward)
            }
        }

        return newState;
    }

private:
    uint8_t currentIndex_;
    uint8_t prevIndex_;
    uint8_t brightness_;
    bool isFadingOut_;
    bool isWaitingForPause_;
    SimpleTimer pauseTimer_;
}; 