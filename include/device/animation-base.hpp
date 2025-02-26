#pragma once

#include "light-interface.hpp"
#include "game/quickdraw-resources.hpp"
#include "simple-timer.hpp"

class AnimationBase : public IAnimation {
public:
    AnimationBase() : 
        isPaused_(false),
        isComplete_(false) {
        frameTimer_.setTimer(16); // Default 16ms between frames
    }
    
    virtual ~AnimationBase() = default;

    void init(const AnimationConfig& config) override {
        config_ = config;
        isPaused_ = false;
        isComplete_ = false;
        frameTimer_.setTimer(config.speed);
        onInit();
    }

    LEDState animate(uint32_t currentTime) override final {
        if (isPaused_ || isComplete_) {
            return currentState_;
        }

        // Only update if enough time has passed
        if (frameTimer_.expired()) {
            frameTimer_.setTimer(config_.speed);  // Reset timer for next frame
            currentState_ = onAnimate(currentTime);
            currentState_.timestamp = currentTime;
        }

        return currentState_;
    }

    bool isComplete() const override { return isComplete_; }
    void pause() override { isPaused_ = true; }
    void resume() override { isPaused_ = false; }
    void stop() override { isComplete_ = true; }
    AnimationType getType() const override { return config_.type; }
    bool isPaused() const override { return isPaused_; }

protected:
    // Override these in derived classes
    virtual void onInit() = 0;
    virtual LEDState onAnimate(uint32_t currentTime) = 0;
    
    // Use the easing curves from quickdraw-resources.hpp
    uint8_t getEasingValue(uint8_t progress, EaseCurve curve) const override {
        // Ensure progress is in bounds
        progress = min(progress, (uint8_t)255);
        
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

    // Helper to set a single LED in the state
    void setLED(LEDState& state, bool isLeft, uint8_t index, const LEDColor& color, uint8_t brightness) {
        if (index >= 9) return;
        
        LEDState::SingleLEDState& led = isLeft ? 
            state.leftLights[index] : 
            state.rightLights[index];
            
        led.color = color;
        led.brightness = brightness;
    }

    // Helper to set both left and right LEDs
    void setLEDPair(LEDState& state, uint8_t index, const LEDColor& color, uint8_t brightness) {
        setLED(state, true, index, color, brightness);
        setLED(state, false, index, color, brightness);
    }

    // Helper to clear all LEDs
    void clearState(LEDState& state) {
        for (int i = 0; i < 9; i++) {
            state.leftLights[i] = LEDState::SingleLEDState();
            state.rightLights[i] = LEDState::SingleLEDState();
        }
        state.transmitLight = LEDState::SingleLEDState();
    }

protected:
    bool isPaused_;
    bool isComplete_;
    SimpleTimer frameTimer_;
    AnimationConfig config_;
    LEDState currentState_;
}; 