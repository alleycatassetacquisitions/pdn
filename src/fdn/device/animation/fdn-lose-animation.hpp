#pragma once

#include "device/animation/animation-base.hpp"
#include "utils/simple-timer.hpp"
#include <algorithm>
#include <random>

class FDNLoseAnimation : public AnimationBase {
public:
    FDNLoseAnimation()
        : AnimationBase()
        , animationState_(AnimationState::SHORT_CIRCUIT)
        , stateStartTime_(0)
        , flickerTimer_(0)
        , fadeProgress_(255) {}

protected:
    void onInit() override {
        animationState_ = AnimationState::SHORT_CIRCUIT;
        stateStartTime_ = SimpleTimer::getPlatformClock()->milliseconds();
        flickerTimer_ = 0;
        fadeProgress_ = 255;

        if (config_.speed == 0) {
            frameTimer_.setTimer(16);
        }

        currentState_ = config_.initialState;
        currentState_.clear();
    }

    LEDState onAnimate() override {
        uint32_t currentTime = SimpleTimer::getPlatformClock()->milliseconds();

        switch (animationState_) {
            case AnimationState::SHORT_CIRCUIT:
                animateShortCircuit(currentTime);
                if (currentTime - stateStartTime_ > 250) {
                    animationState_ = AnimationState::POWER_DOWN;
                    stateStartTime_ = currentTime;
                }
                break;

            case AnimationState::POWER_DOWN:
                animatePowerDown(currentTime);
                if (currentTime - stateStartTime_ > 1500) {
                    animationState_ = AnimationState::WARNING;
                    stateStartTime_ = currentTime;
                }
                break;

            case AnimationState::WARNING:
                animateWarning(currentTime);
                break;
        }

        return currentState_;
    }

private:
    enum class AnimationState {
        SHORT_CIRCUIT,
        POWER_DOWN,
        WARNING,
    };

    void animateShortCircuit(uint32_t currentTime) {
        currentState_.clear();

        if (currentTime - flickerTimer_ > 80 + randomInt(0, 49)) {
            flickerTimer_ = currentTime;

            for (int i = 0; i < 9; i++) {
                bool leftLit = randomInt(0, 99) < 25;
                bool rightLit = randomInt(0, 99) < 25;

                LEDColor color;
                int colorChoice = randomInt(0, 2);
                if (colorChoice == 0) {
                    color = LEDColor(150, 150, 150);
                } else if (colorChoice == 1) {
                    color = LEDColor(90, 90, 150);
                } else {
                    color = LEDColor(150, 150, 40);
                }

                uint8_t brightness = 50 + randomInt(0, 100);

                if (leftLit) {
                    currentState_.leftLights[i] = LEDState::SingleLEDState(color, brightness);
                }
                if (rightLit) {
                    currentState_.rightLights[i] = LEDState::SingleLEDState(color, brightness);
                }
            }

            if (randomInt(0, 99) < 15) {
                LEDColor surgeColor(150, 150, 150);
                for (int i = 0; i < 9; i++) {
                    if (randomInt(0, 99) < 40) {
                        currentState_.setLEDPair(i, surgeColor, 130);
                    }
                }
            }
        }
    }

    void animatePowerDown(uint32_t currentTime) {
        (void)currentTime;

        if (fadeProgress_ > 0) {
            fadeProgress_ = std::max(0, fadeProgress_ - 3);
            LEDColor powerDownColor(100, 100, 150);

            for (int i = 0; i < 9; i++) {
                currentState_.setLEDPair(i, powerDownColor, fadeProgress_);
            }

            if (randomInt(0, 99) < 5) {
                int flickerLED = randomInt(0, 8);
                int totalBrightness = fadeProgress_ + randomInt(0, 99);
                uint8_t flickerBrightness =
                    (totalBrightness > 255) ? 255 : static_cast<uint8_t>(totalBrightness);
                currentState_.setLEDPair(flickerLED, powerDownColor, flickerBrightness);
            }
        }
    }

    static int randomInt(int min, int max) {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        std::uniform_int_distribution<> dist(min, max);
        return dist(gen);
    }

    void animateWarning(uint32_t currentTime) {
        currentState_.clear();

        uint32_t elapsedTime = currentTime - stateStartTime_;
        uint32_t cycleCount = elapsedTime / 1000;

        float dimmingFactor = 1.0f;
        if (cycleCount >= 3) {
            dimmingFactor = 0.1f;
        } else if (cycleCount >= 2) {
            dimmingFactor = 0.3f;
        } else if (cycleCount >= 1) {
            dimmingFactor = 0.6f;
        }

        uint32_t warningPhase = elapsedTime % 1000;
        uint8_t brightness;
        if (warningPhase < 500) {
            brightness = 50 + warningPhase * 0.4;
        } else {
            brightness = 50 + (1000 - warningPhase) * 0.4;
        }

        brightness = static_cast<uint8_t>(brightness * dimmingFactor);

        LEDColor amberColor(255, 120, 0);
        LEDColor redColor(255, 0, 0);

        currentState_.leftLights[1] = LEDState::SingleLEDState(amberColor, brightness);
        currentState_.leftLights[4] = LEDState::SingleLEDState(amberColor, brightness);
        currentState_.leftLights[7] = LEDState::SingleLEDState(amberColor, brightness);

        currentState_.rightLights[1] = LEDState::SingleLEDState(redColor, brightness);
        currentState_.rightLights[4] = LEDState::SingleLEDState(redColor, brightness);
        currentState_.rightLights[7] = LEDState::SingleLEDState(redColor, brightness);

        if (warningPhase < 200 || (warningPhase > 500 && warningPhase < 700)) {
            currentState_.transmitLight = LEDState::SingleLEDState(redColor, brightness);
        }

        if (!config_.loop && cycleCount >= 3 && warningPhase > 900) {
            isComplete_ = true;
        }
    }

    AnimationState animationState_;
    uint32_t stateStartTime_;
    uint32_t flickerTimer_;
    int fadeProgress_;
};
