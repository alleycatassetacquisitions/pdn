#pragma once

#include "animation-base.hpp"
#include "utils/simple-timer.hpp"
#include <algorithm>
#include <random>

class LoseAnimation : public AnimationBase {
public:
    LoseAnimation() : 
        AnimationBase(),
        animationState_(AnimationState::SHORT_CIRCUIT),
        stateStartTime_(0),
        flickerTimer_(0),
        fadeProgress_(255) {
        LOG_I("LoseAnimation", "Constructor called");
    }

protected:
    void onInit() override {
        LOG_I("LoseAnimation", "onInit called");
        // Reset animation state
        animationState_ = AnimationState::SHORT_CIRCUIT;
        stateStartTime_ = SimpleTimer::getPlatformClock()->milliseconds();
        flickerTimer_ = 0;
        fadeProgress_ = 255;
        
        // Set default frame rate for smooth animation (16ms per frame)
        if (config_.speed == 0) {
            frameTimer_.setTimer(16);
        }
        
        // Initialize with clear state
        currentState_ = config_.initialState;
        currentState_.clear();
        LOG_I("LoseAnimation", "Initialized with frame timer: %d ms", config_.speed);
    }

    LEDState onAnimate() override {
        uint32_t currentTime = SimpleTimer::getPlatformClock()->milliseconds();
        
        switch (animationState_) {
            case AnimationState::SHORT_CIRCUIT:
                // Short circuit effect: rapid flickering with bright flashes
                animateShortCircuit(currentTime);
                
                // Transition to power down after just 0.25 seconds (reduced from 0.5 seconds)
                if (currentTime - stateStartTime_ > 250) {
                    animationState_ = AnimationState::POWER_DOWN;
                    stateStartTime_ = currentTime;
                    LOG_I("LoseAnimation", "Transitioning to POWER_DOWN state");
                }
                break;
                
            case AnimationState::POWER_DOWN:
                // Power down effect: gradual dimming with occasional warning flickers
                animatePowerDown(currentTime);
                
                // Transition to warning state after power down (1.5 seconds)
                if (currentTime - stateStartTime_ > 1500) {
                    animationState_ = AnimationState::WARNING;
                    stateStartTime_ = currentTime;
                    LOG_I("LoseAnimation", "Transitioning to WARNING state");
                }
                break;
                
            case AnimationState::WARNING:
                // Warning effect: amber/red warning lights that dim over time
                // The animateWarning method handles timing and completion logic
                animateWarning(currentTime);
                break;
        }
        
        return currentState_;
    }

private:
    enum class AnimationState {
        SHORT_CIRCUIT,
        POWER_DOWN,
        WARNING
    };
    
    void animateShortCircuit(uint32_t currentTime) {
        // Clear previous state
        currentState_.clear();
        
        // Even slower, less intense flickering
        if (currentTime - flickerTimer_ > 80 + randomInt(0, 49)) {  // Further increased from 60+random(40)
            flickerTimer_ = currentTime;
            
            // Randomize which LEDs light up
            for (int i = 0; i < 9; i++) {
                // 25% chance for each LED to light up (further reduced from 30%)
                bool leftLit = randomInt(0, 99) < 25;
                bool rightLit = randomInt(0, 99) < 25;
                
                // Further reduced color intensity
                LEDColor color;
                int colorChoice = randomInt(0, 2);
                if (colorChoice == 0) {
                    color = LEDColor(150, 150, 150); // Even dimmer white (reduced from 200)
                } else if (colorChoice == 1) {
                    color = LEDColor(90, 90, 150); // Even dimmer blue-white (reduced from 130)
                } else {
                    color = LEDColor(150, 150, 40);  // Even dimmer yellow (reduced from 200)
                }
                
                // Randomize brightness between 50-150 (reduced from 100-200)
                uint8_t brightness = 50 + randomInt(0, 100);
                
                // Set LEDs
                if (leftLit) {
                    currentState_.leftLights[i] = LEDState::SingleLEDState(color, brightness);
                }
                if (rightLit) {
                    currentState_.rightLights[i] = LEDState::SingleLEDState(color, brightness);
                }
            }
            
            // Even less frequent surges (15% chance instead of 20%)
            if (randomInt(0, 99) < 15) {
                LEDColor surgeColor(150, 150, 150); // Even dimmer surge (reduced from 200)
                // Fewer LEDs light up during a surge
                for (int i = 0; i < 9; i++) {
                    if (randomInt(0, 99) < 40) { // 40% chance instead of 50%
                        currentState_.setLEDPair(i, surgeColor, 130); // Fixed lower brightness (reduced from 180)
                    }
                }
            }
        }
    }
    
    void animatePowerDown(uint32_t currentTime) {
        // Gradual fade out of all LEDs
        if (fadeProgress_ > 0) {
            fadeProgress_ = std::max(0, fadeProgress_ - 3);
            
            // Set all LEDs to a dim blue-ish white with decreasing brightness
            LEDColor powerDownColor(100, 100, 150);
            
            for (int i = 0; i < 9; i++) {
                currentState_.setLEDPair(i, powerDownColor, fadeProgress_);
            }
            
            // Occasionally add a flicker during power down
            if (randomInt(0, 99) < 5) {
                int flickerLED = randomInt(0, 8);
                // Calculate flicker brightness with bounds checking
                int extraBrightness = randomInt(0, 99);
                int totalBrightness = fadeProgress_ + extraBrightness;
                uint8_t flickerBrightness = (totalBrightness > 255) ? 255 : static_cast<uint8_t>(totalBrightness);
                
                currentState_.setLEDPair(flickerLED, powerDownColor, flickerBrightness);
            }
        }
    }
    
    // Helper function for random integers in range [min, max]
    static int randomInt(int min, int max) {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        std::uniform_int_distribution<> dist(min, max);
        return dist(gen);
    }
    
    void animateWarning(uint32_t currentTime) {
        // Clear all LEDs first
        currentState_.clear();
        
        // Calculate elapsed time and cycle count
        uint32_t elapsedTime = currentTime - stateStartTime_;
        uint32_t cycleCount = elapsedTime / 1000; // Each cycle is 1000ms
        
        // Calculate dimming factor based on cycle count (0-100%)
        // By the third cycle, lights will be very dim
        float dimmingFactor = 1.0f;
        if (cycleCount >= 3) {
            dimmingFactor = 0.1f; // 10% brightness
        } else if (cycleCount >= 2) {
            dimmingFactor = 0.3f; // 30% brightness
        } else if (cycleCount >= 1) {
            dimmingFactor = 0.6f; // 60% brightness
        }
        
        // Slow pulsing amber/red warning lights
        uint32_t warningPhase = elapsedTime % 1000;
        uint8_t brightness;
        
        // Calculate brightness for slow pulse
        if (warningPhase < 500) {
            brightness = 50 + warningPhase * 0.4; // Gradually increase
        } else {
            brightness = 50 + (1000 - warningPhase) * 0.4; // Gradually decrease
        }
        
        // Apply dimming factor
        brightness = static_cast<uint8_t>(brightness * dimmingFactor);
        
        // Apply warning lights - amber on left side, red on right side
        LEDColor amberColor(255, 120, 0);
        LEDColor redColor(255, 0, 0);
        
        // Display only a few warning lights
        currentState_.leftLights[1] = LEDState::SingleLEDState(amberColor, brightness);
        currentState_.leftLights[4] = LEDState::SingleLEDState(amberColor, brightness);
        currentState_.leftLights[7] = LEDState::SingleLEDState(amberColor, brightness);
        
        currentState_.rightLights[1] = LEDState::SingleLEDState(redColor, brightness);
        currentState_.rightLights[4] = LEDState::SingleLEDState(redColor, brightness);
        currentState_.rightLights[7] = LEDState::SingleLEDState(redColor, brightness);
        
        // Transmit light occasional blink - red
        if (warningPhase < 200 || (warningPhase > 500 && warningPhase < 700)) {
            currentState_.transmitLight = LEDState::SingleLEDState(redColor, brightness);
        }
        
        // If we've completed 3 full cycles and we're not looping, mark as complete
        if (!config_.loop && cycleCount >= 3 && warningPhase > 900) {
            isComplete_ = true;
            LOG_I("LoseAnimation", "Animation complete after 3 warning cycles");
        }
    }
    
    AnimationState animationState_;
    uint32_t stateStartTime_;
    uint32_t flickerTimer_;
    int fadeProgress_;
};