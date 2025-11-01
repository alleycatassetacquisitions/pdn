#pragma once

#include "animation.hpp"
#include "simple-timer.hpp"
#include <algorithm> // For std::min
#include "esp_log.h"

class VerticalChaseAnimation : public Animation {
public:
    VerticalChaseAnimation() : 
        Animation(),
        currentIndex(12),  // Start at top
        progress(0),
        isWaitingForPause(false) {
        // Initialize with 0ms timer (expired)
        pauseTimer.setTimer(0);
    }

protected:
    void onInit() override {
        //currentIndex starts at 12.
        currentIndex = 12;
        progress = 0;
        isWaitingForPause = false;

        ledColors[0] = config.initialState.leftLights[0].color;
        ledColors[1] = config.initialState.leftLights[1].color;
        ledColors[2] = config.initialState.leftLights[2].color;
        ledColors[3] = config.initialState.leftLights[3].color;
        
        pauseTimer.setTimer(0);  // Reset pause timer
        currentState = config.initialState; //Set the current state to initial.
    }

    LEDState onAnimate() override {
        // If we're waiting for a pause between animations
        if (isWaitingForPause) {
            if (pauseTimer.expired()) {
                // Reset for next animation cycle
                isWaitingForPause = false;
                currentIndex = 12;  // Reset to top
                progress = 0;
            } else {
                return currentState;
            }
        }

        // Increment progress for fade in/out and interpolation.
        progress = min(progress + 20, 255);  // Adjust speed as needed

        if(progress > 255) {
            progress = 255;
        }

        int currentBezierValue = getEasingValue(progress, config.curve);
        
        switch(currentIndex) {
            case 12:
                currentState.setLEDPair(8, interpolateColor(currentState.leftLights[8].color, ledColors[0], currentBezierValue), 35);
                break;
            case 11:
                currentState.setLEDPair(8, interpolateColor(currentState.leftLights[8].color, ledColors[1], currentBezierValue), 35);
                currentState.setLEDPair(7, interpolateColor(currentState.leftLights[7].color, ledColors[0], currentBezierValue), min(currentBezierValue, 75));
                break;
            case 10:
                currentState.setLEDPair(8, interpolateColor(currentState.leftLights[8].color, ledColors[2], currentBezierValue), 35);
                currentState.setLEDPair(7, interpolateColor(currentState.leftLights[7].color, ledColors[1], currentBezierValue), 35);
                currentState.setLEDPair(6, interpolateColor(currentState.leftLights[6].color, ledColors[0], currentBezierValue), min(currentBezierValue, 75));
                break;
            case 9:
                currentState.setLEDPair(8, interpolateColor(currentState.leftLights[8].color, ledColors[3], currentBezierValue), min(255-currentBezierValue, 35));
                currentState.setLEDPair(7, interpolateColor(currentState.leftLights[7].color, ledColors[2], currentBezierValue), 35);
                currentState.setLEDPair(6, interpolateColor(currentState.leftLights[6].color, ledColors[1], currentBezierValue), 35);
                currentState.setLEDPair(5, interpolateColor(currentState.leftLights[5].color, ledColors[0], currentBezierValue), min(currentBezierValue, 75));
                break;
            case 8:
                currentState.setLEDPair(7, interpolateColor(currentState.leftLights[7].color, ledColors[3], currentBezierValue), min(255-currentBezierValue, 35));
                currentState.setLEDPair(6, interpolateColor(currentState.leftLights[6].color, ledColors[2], currentBezierValue), 35);
                currentState.setLEDPair(5, interpolateColor(currentState.leftLights[5].color, ledColors[1], currentBezierValue), 35);
                currentState.setLEDPair(4, interpolateColor(currentState.leftLights[4].color, ledColors[0], currentBezierValue), min(currentBezierValue, 75));
                break;
            case 7:
                currentState.setLEDPair(6, interpolateColor(currentState.leftLights[6].color, ledColors[3], currentBezierValue), min(255-currentBezierValue, 35));
                currentState.setLEDPair(5, interpolateColor(currentState.leftLights[5].color, ledColors[2], currentBezierValue), 35);
                currentState.setLEDPair(4, interpolateColor(currentState.leftLights[4].color, ledColors[1], currentBezierValue), 35);
                currentState.setLEDPair(3, interpolateColor(currentState.leftLights[3].color, ledColors[0], currentBezierValue), min(currentBezierValue, 75));
                break;
            case 6:
                currentState.setLEDPair(5, interpolateColor(currentState.leftLights[5].color, ledColors[3], currentBezierValue), min(255-currentBezierValue, 35));
                currentState.setLEDPair(4, interpolateColor(currentState.leftLights[4].color, ledColors[2], currentBezierValue), 35);
                currentState.setLEDPair(3, interpolateColor(currentState.leftLights[3].color, ledColors[1], currentBezierValue), 35);
                currentState.setLEDPair(2, interpolateColor(currentState.leftLights[2].color, ledColors[0], currentBezierValue), min(currentBezierValue, 75));
                break;
            case 5:
                currentState.setLEDPair(4, interpolateColor(currentState.leftLights[4].color, ledColors[3], currentBezierValue), min(255-currentBezierValue, 35));
                currentState.setLEDPair(3, interpolateColor(currentState.leftLights[3].color, ledColors[3], currentBezierValue), 35);
                currentState.setLEDPair(2, interpolateColor(currentState.leftLights[2].color, ledColors[2], currentBezierValue), 35);
                currentState.setLEDPair(1, interpolateColor(currentState.leftLights[1].color, ledColors[1], currentBezierValue), min(currentBezierValue, 75));
                break;
            case 4:
                currentState.setLEDPair(3, interpolateColor(currentState.leftLights[3].color, ledColors[3], currentBezierValue), min(255-currentBezierValue, 35));
                currentState.setLEDPair(2, interpolateColor(currentState.leftLights[2].color, ledColors[2], currentBezierValue), 35);
                currentState.setLEDPair(1, interpolateColor(currentState.leftLights[1].color, ledColors[1], currentBezierValue), 35);
                currentState.setLEDPair(0, interpolateColor(currentState.leftLights[0].color, ledColors[0], currentBezierValue), min(currentBezierValue, 75));
                break;
            case 3:
                currentState.setLEDPair(2, interpolateColor(currentState.leftLights[2].color, ledColors[3], currentBezierValue), min(255-currentBezierValue, 35));
                currentState.setLEDPair(1, interpolateColor(currentState.leftLights[1].color, ledColors[2], currentBezierValue), 35);
                currentState.setLEDPair(0, interpolateColor(currentState.leftLights[0].color, ledColors[1], currentBezierValue), 35);
                break;
            case 2:
                currentState.setLEDPair(1, interpolateColor(currentState.leftLights[1].color, ledColors[3], currentBezierValue), min(255-currentBezierValue, 35));
                currentState.setLEDPair(0, interpolateColor(currentState.leftLights[0].color, ledColors[2], currentBezierValue), 35);
                break;
            case 1:
                currentState.setLEDPair(0, interpolateColor(currentState.leftLights[0].color, ledColors[3], currentBezierValue), min(255-currentBezierValue, 35));
                break;
            default:
                break;
        }
        
        if (progress == 255) {
            progress = 0;
            currentIndex--;

            // If we've reached the end of the animation
            if (currentIndex <= 0) {
                // If looping is enabled, set up for the next loop
                if (config.loop) {
                    isWaitingForPause = true;
                    pauseTimer.setTimer(config.loopDelayMs);
                } else {
                    isComplete = true;
                }
            }
        }

        return currentState;
    }

private:
    LEDColor ledColors[4];
    uint8_t currentIndex;
    uint8_t progress;
    bool isWaitingForPause;
    SimpleTimer pauseTimer;
    uint8_t leadingBrightness = 255;
    float trailingBrightnessFactor = .2;
}; 