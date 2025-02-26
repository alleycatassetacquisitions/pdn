#pragma once

#include "animation-base.hpp"
#include "simple-timer.hpp"
#include <algorithm> // For std::min

class IdleAnimation : public AnimationBase {
public:
    IdleAnimation() : 
        AnimationBase(),
        currentIndex_(8),  // Start at top
        brightness_(0),
        isFadingOut_(false),
        isWaitingForPause_(false) {
        // Initialize with 0ms timer (expired)
        pauseTimer_.setTimer(0);
        
        // Blue color scheme
        primaryColor_ = LEDColor(0, 150, 255);  // Blue
        trailColor1_ = LEDColor(0, 50, 255);    // Lighter blue
        trailColor2_ = LEDColor(0, 0, 150);     // Darker blue
    }

protected:
    void onInit() override {
        currentIndex_ = 8;  // Start at top
        brightness_ = 0;
        isFadingOut_ = false;
        isWaitingForPause_ = false;
        
        // If a palette is available, use those colors
        if (hasPalette()) {
            primaryColor_ = getPaletteColor(0);
            trailColor1_ = getPaletteColor(1 % getPaletteSize());
            trailColor2_ = getPaletteColor(2 % getPaletteSize());
        }
        
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
                brightness_ = 0;
            }
            return newState;  // Return empty state during pause
        }

        // Normal animation sequence with 3 LEDs in the chase
        // Set the current LED and two trailing LEDs with decreasing brightness
        setLEDPair(newState, currentIndex_, primaryColor_, brightness_);
        
        // First trailing LED (one position behind)
        if (currentIndex_ < 8) {
            uint8_t trailingBrightness1 = std::min<uint8_t>(255, brightness_ * 0.7);
            setLEDPair(newState, currentIndex_ + 1, trailColor1_, trailingBrightness1);
        }
        
        // Second trailing LED (two positions behind)
        if (currentIndex_ < 7) {
            uint8_t trailingBrightness2 = std::min<uint8_t>(255, brightness_ * 0.4);
            setLEDPair(newState, currentIndex_ + 2, trailColor2_, trailingBrightness2);
        }

        for(int i = 0; i < 9; i++) {
            if(!(currentIndex_ == i || currentIndex_ + 1 == i || currentIndex_ + 2 == i)) {
                setLEDPair(newState, i, LEDColor(0, 0, 0), 0);
            }
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
                isWaitingForPause_ = true;
                pauseTimer_.setTimer(250);
            } else {
                brightness_ = 0;  // Reset brightness for next position
                currentIndex_--;  // Move to next position (moving downward)
            }
        }

        return newState;
    }

private:
    uint8_t currentIndex_;
    uint8_t brightness_;
    bool isFadingOut_;
    bool isWaitingForPause_;
    LEDColor primaryColor_;
    LEDColor trailColor1_;
    LEDColor trailColor2_;
    SimpleTimer pauseTimer_;
}; 