#pragma once

#include "light-interface.hpp"
#include "game/quickdraw-resources.hpp"
#include "simple-timer.hpp"
#include <algorithm> // For std::min
#include "esp_log.h"

class AnimationBase : public IAnimation {
public:
    AnimationBase() : 
        isPaused(false),
        isComplete(false) {
        frameTimer.setTimer(16); // Default 16ms between frames
    }
    
    virtual ~AnimationBase() {
    }

    void init(const AnimationConfig& config) override {
        this->config = config;
        isPaused = false;
        isComplete = false;
        frameTimer.setTimer(config.speed);
        
        // Initialize with the provided initial state
        currentState = config.initialState;
        
        onInit();
    }

    LEDState animate() override final {
        if (isPaused || isComplete) {
            return currentState;
        }

        // Only update if enough time has passed
        if (frameTimer.expired()) {
            frameTimer.setTimer(config.speed);  // Reset timer for next frame
            
            // Update the current state directly and get it back
            LEDState prevState = currentState;
            currentState = onAnimate();
            currentState.timestamp = millis();
        }

        return currentState;
    }

    bool complete() const override { return isComplete; }
    
    void pause() override { 
        isPaused = true; 
    }
    
    void resume() override { 
        isPaused = false; 
    }
    
    void stop() override { 
        isComplete = true; 
    }
    
    AnimationType getType() const override { return config.type; }
    bool paused() const override { return isPaused; }

protected:
    // Override these in derived classes
    virtual void onInit() = 0;
    
    // Returns the updated state (which is also stored in currentState_)
    virtual LEDState onAnimate() = 0;
    
    // Use the easing curves from quickdraw-resources.hpp
    uint8_t getEasingValue(uint8_t progress, EaseCurve curve) const override {
        // Ensure progress is in bounds
        progress = std::min(progress, (uint8_t)255);
        
        // Return the appropriate easing value from lookup tables
        switch (curve) {
            case EaseCurve::LINEAR:
                return LINEAR_CURVE[progress];
            case EaseCurve::EASE_IN_OUT:
                return EASE_IN_OUT_CURVE[progress];
            case EaseCurve::EASE_OUT:
                return EASE_OUT_CURVE[progress];
            case EaseCurve::ELASTIC:
                return ELASTIC_CURVE[progress];
            default:
                return LINEAR_CURVE[progress];
        }
    }
    
    // Interpolate between two colors based on progress (t is 0-255)
    LEDColor interpolateColor(const LEDColor& start, const LEDColor& end, uint8_t t) const {
        // Use fixed-point math for interpolation (t is 0-255)
        return LEDColor(
            start.red + ((end.red - start.red) * t) / 255,
            start.green + ((end.green - start.green) * t) / 255,
            start.blue + ((end.blue - start.blue) * t) / 255
        );
    }

protected:
    bool isPaused;
    bool isComplete;
    SimpleTimer frameTimer;
    AnimationConfig config;
    LEDState currentState;
}; 