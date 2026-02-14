#pragma once

#include "drivers/light-interface.hpp"
#include "game/quickdraw-resources.hpp"
#include "utils/simple-timer.hpp"
#include <algorithm> // For std::min

class AnimationBase : public IAnimation {
public:
    AnimationBase() : 
        isPaused_(false),
        isComplete_(false) {
        frameTimer_.setTimer(16); // Default 16ms between frames
    }
    
    ~AnimationBase() override = default;

    void init(const AnimationConfig& config) override {
        config_ = config;
        isPaused_ = false;
        isComplete_ = false;
        frameTimer_.setTimer(config.speed);
        
        // Initialize with the provided initial state
        currentState_ = config.initialState;
        
        onInit();
    }

    LEDState animate() override final {
        if (isPaused_ || isComplete_) {
            return currentState_;
        }

        // Only update if enough time has passed
        if (frameTimer_.expired()) {
            frameTimer_.setTimer(config_.speed);  // Reset timer for next frame
            
            // Update the current state directly and get it back
            LEDState prevState = currentState_;
            currentState_ = onAnimate();
            currentState_.timestamp = SimpleTimer::getPlatformClock()->milliseconds();
        }

        return currentState_;
    }

    bool isComplete() const override { return isComplete_; }
    
    void pause() override { 
        isPaused_ = true; 
    }
    
    void resume() override { 
        isPaused_ = false; 
    }
    
    void stop() override { 
        isComplete_ = true; 
    }
    
    AnimationType getType() const override { return config_.type; }
    bool isPaused() const override { return isPaused_; }

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
    bool isPaused_;
    bool isComplete_;
    SimpleTimer frameTimer_;
    AnimationConfig config_;
    LEDState currentState_;
};