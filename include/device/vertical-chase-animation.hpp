#pragma once

#include "animation-base.hpp"
#include "utils/simple-timer.hpp"
#include <algorithm> // For std::min

class VerticalChaseAnimation : public AnimationBase {
public:
    VerticalChaseAnimation() : 
        AnimationBase(),
        currentIndex_(12),  // Start at top
        progress(0),
        isWaitingForPause_(false) {
        // Initialize with 0ms timer (expired)
        pauseTimer_.setTimer(0);
    }

protected:
    void onInit() override {
        //currentIndex starts at 12.
        currentIndex_ = 12;
        progress = 0;
        isWaitingForPause_ = false;

        ledColors[0] = config_.initialState.leftLights[0].color;
        ledColors[1] = config_.initialState.leftLights[1].color;
        ledColors[2] = config_.initialState.leftLights[2].color;
        ledColors[3] = config_.initialState.leftLights[3].color;
        
        pauseTimer_.setTimer(0);  // Reset pause timer
        currentState_ = config_.initialState; //Set the current state to initial.
    }

    LEDState onAnimate() override {
        // If we're waiting for a pause between animations
        if (isWaitingForPause_) {
            if (pauseTimer_.expired()) {
                // Reset for next animation cycle
                isWaitingForPause_ = false;
                currentIndex_ = 12;  // Reset to top
                progress = 0;
            } else {
                return currentState_;
            }
        }

        // Increment progress for fade in/out and interpolation.
        progress = std::min(progress + 20, 255);  // Adjust speed as needed

        if(progress > 255) {
            progress = 255;
        }

        int currentBezierValue = getEasingValue(progress, config_.curve);
        
        switch(currentIndex_) {
            case 12:
                currentState_.setLEDPair(8, interpolateColor(currentState_.leftLights[8].color, ledColors[0], currentBezierValue), 35);
                break;
            case 11:
                currentState_.setLEDPair(8, interpolateColor(currentState_.leftLights[8].color, ledColors[1], currentBezierValue), 35);
                currentState_.setLEDPair(7, interpolateColor(currentState_.leftLights[7].color, ledColors[0], currentBezierValue), std::min(currentBezierValue, 75));
                break;
            case 10:
                currentState_.setLEDPair(8, interpolateColor(currentState_.leftLights[8].color, ledColors[2], currentBezierValue), 35);
                currentState_.setLEDPair(7, interpolateColor(currentState_.leftLights[7].color, ledColors[1], currentBezierValue), 35);
                currentState_.setLEDPair(6, interpolateColor(currentState_.leftLights[6].color, ledColors[0], currentBezierValue), std::min(currentBezierValue, 75));
                break;
            case 9:
                currentState_.setLEDPair(8, interpolateColor(currentState_.leftLights[8].color, ledColors[3], currentBezierValue), std::min(255-currentBezierValue, 35));
                currentState_.setLEDPair(7, interpolateColor(currentState_.leftLights[7].color, ledColors[2], currentBezierValue), 35);
                currentState_.setLEDPair(6, interpolateColor(currentState_.leftLights[6].color, ledColors[1], currentBezierValue), 35);
                currentState_.setLEDPair(5, interpolateColor(currentState_.leftLights[5].color, ledColors[0], currentBezierValue), std::min(currentBezierValue, 75));
                break;
            case 8:
                currentState_.setLEDPair(7, interpolateColor(currentState_.leftLights[7].color, ledColors[3], currentBezierValue), std::min(255-currentBezierValue, 35));
                currentState_.setLEDPair(6, interpolateColor(currentState_.leftLights[6].color, ledColors[2], currentBezierValue), 35);
                currentState_.setLEDPair(5, interpolateColor(currentState_.leftLights[5].color, ledColors[1], currentBezierValue), 35);
                currentState_.setLEDPair(4, interpolateColor(currentState_.leftLights[4].color, ledColors[0], currentBezierValue), std::min(currentBezierValue, 75));
                break;
            case 7:
                currentState_.setLEDPair(6, interpolateColor(currentState_.leftLights[6].color, ledColors[3], currentBezierValue), std::min(255-currentBezierValue, 35));
                currentState_.setLEDPair(5, interpolateColor(currentState_.leftLights[5].color, ledColors[2], currentBezierValue), 35);
                currentState_.setLEDPair(4, interpolateColor(currentState_.leftLights[4].color, ledColors[1], currentBezierValue), 35);
                currentState_.setLEDPair(3, interpolateColor(currentState_.leftLights[3].color, ledColors[0], currentBezierValue), std::min(currentBezierValue, 75));
                break;
            case 6:
                currentState_.setLEDPair(5, interpolateColor(currentState_.leftLights[5].color, ledColors[3], currentBezierValue), std::min(255-currentBezierValue, 35));
                currentState_.setLEDPair(4, interpolateColor(currentState_.leftLights[4].color, ledColors[2], currentBezierValue), 35);
                currentState_.setLEDPair(3, interpolateColor(currentState_.leftLights[3].color, ledColors[1], currentBezierValue), 35);
                currentState_.setLEDPair(2, interpolateColor(currentState_.leftLights[2].color, ledColors[0], currentBezierValue), std::min(currentBezierValue, 75));
                break;
            case 5:
                currentState_.setLEDPair(4, interpolateColor(currentState_.leftLights[4].color, ledColors[3], currentBezierValue), std::min(255-currentBezierValue, 35));
                currentState_.setLEDPair(3, interpolateColor(currentState_.leftLights[3].color, ledColors[3], currentBezierValue), 35);
                currentState_.setLEDPair(2, interpolateColor(currentState_.leftLights[2].color, ledColors[2], currentBezierValue), 35);
                currentState_.setLEDPair(1, interpolateColor(currentState_.leftLights[1].color, ledColors[1], currentBezierValue), std::min(currentBezierValue, 75));
                break;
            case 4:
                currentState_.setLEDPair(3, interpolateColor(currentState_.leftLights[3].color, ledColors[3], currentBezierValue), std::min(255-currentBezierValue, 35));
                currentState_.setLEDPair(2, interpolateColor(currentState_.leftLights[2].color, ledColors[2], currentBezierValue), 35);
                currentState_.setLEDPair(1, interpolateColor(currentState_.leftLights[1].color, ledColors[1], currentBezierValue), 35);
                currentState_.setLEDPair(0, interpolateColor(currentState_.leftLights[0].color, ledColors[0], currentBezierValue), std::min(currentBezierValue, 75));
                break;
            case 3:
                currentState_.setLEDPair(2, interpolateColor(currentState_.leftLights[2].color, ledColors[3], currentBezierValue), std::min(255-currentBezierValue, 35));
                currentState_.setLEDPair(1, interpolateColor(currentState_.leftLights[1].color, ledColors[2], currentBezierValue), 35);
                currentState_.setLEDPair(0, interpolateColor(currentState_.leftLights[0].color, ledColors[1], currentBezierValue), 35);
                break;
            case 2:
                currentState_.setLEDPair(1, interpolateColor(currentState_.leftLights[1].color, ledColors[3], currentBezierValue), std::min(255-currentBezierValue, 35));
                currentState_.setLEDPair(0, interpolateColor(currentState_.leftLights[0].color, ledColors[2], currentBezierValue), 35);
                break;
            case 1:
                currentState_.setLEDPair(0, interpolateColor(currentState_.leftLights[0].color, ledColors[3], currentBezierValue), std::min(255-currentBezierValue, 35));
                break;
            default:
                break;
        }
        
        if (progress == 255) {
            progress = 0;
            currentIndex_--;

            // If we've reached the end of the animation
            if (currentIndex_ <= 0) {
                // If looping is enabled, set up for the next loop
                if (config_.loop) {
                    isWaitingForPause_ = true;
                    pauseTimer_.setTimer(config_.loopDelayMs);
                } else {
                    isComplete_ = true;
                }
            }
        }

        return currentState_;
    }

private:
    LEDColor ledColors[4];
    uint8_t currentIndex_;
    uint8_t progress;
    bool isWaitingForPause_;
    SimpleTimer pauseTimer_;
    uint8_t leadingBrightness = 255;
    float trailingBrightnessFactor = .2;
};