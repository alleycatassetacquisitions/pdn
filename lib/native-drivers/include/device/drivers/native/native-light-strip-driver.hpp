#pragma once

#include "device/drivers/driver-interface.hpp"
#include <array>

class NativeLightStripDriver : public LightDriverInterface {
public:
    static constexpr uint8_t MIN_VISIBLE_BRIGHTNESS = 85;  // Floor for non-zero brightness in CLI
    
    explicit NativeLightStripDriver(const std::string& name) : LightDriverInterface(name) {
        clear();
    }

    ~NativeLightStripDriver() override = default;

    int initialize() override {
        return 0;
    }

    void exec() override {
        // In a real implementation, this would push updates to hardware
        // For native, we just store state for dashboard to read
    }
    
    // Apply brightness floor for CLI visibility (low values are invisible in terminal)
    LEDState::SingleLEDState applyBrightnessFloor(LEDState::SingleLEDState led) {
        if (led.brightness > 0 && led.brightness < MIN_VISIBLE_BRIGHTNESS) {
            led.brightness = MIN_VISIBLE_BRIGHTNESS;
        }
        return led;
    }

    void setLight(LightIdentifier lightSet, uint8_t index, LEDState::SingleLEDState color) override {
        color = applyBrightnessFloor(color);
        switch (lightSet) {
            case LightIdentifier::LEFT_LIGHTS:
                if (index < 9) leftLights_[index] = color;
                break;
            case LightIdentifier::RIGHT_LIGHTS:
                if (index < 9) rightLights_[index] = color;
                break;
            case LightIdentifier::TRANSMIT_LIGHT:
                transmitLight_ = color;
                break;
            case LightIdentifier::DISPLAY_LIGHTS:
                // Display lights mapping from LightManager::mapStateToDisplayLights:
                // index 0-5 = left[3..8], index 6-11 = right[8..3], index 12 = transmit
                if (index < 6) {
                    leftLights_[index + 3] = color;  // index 0->3, 1->4, ... 5->8
                } else if (index < 12) {
                    rightLights_[14 - index] = color;  // index 6->8, 7->7, ... 11->3
                } else if (index == 12) {
                    transmitLight_ = color;
                }
                break;
            case LightIdentifier::GRIP_LIGHTS:
                // Grip lights mapping from LightManager::mapStateToGripLights:
                // index 0-2 = left[2..0], index 3-5 = right[0..2]
                if (index < 3) {
                    leftLights_[2 - index] = color;  // index 0->2, 1->1, 2->0
                } else if (index < 6) {
                    rightLights_[index - 3] = color;  // index 3->0, 4->1, 5->2
                }
                break;
            case LightIdentifier::GLOBAL:
                for (int i = 0; i < 9; i++) {
                    leftLights_[i] = color;
                    rightLights_[i] = color;
                }
                transmitLight_ = color;
                break;
        }
    }

    void setLightBrightness(LightIdentifier lightSet, uint8_t index, uint8_t brightness) override {
        switch (lightSet) {
            case LightIdentifier::LEFT_LIGHTS:
                if (index < 9) leftLights_[index].brightness = brightness;
                break;
            case LightIdentifier::RIGHT_LIGHTS:
                if (index < 9) rightLights_[index].brightness = brightness;
                break;
            case LightIdentifier::TRANSMIT_LIGHT:
                transmitLight_.brightness = brightness;
                break;
            case LightIdentifier::DISPLAY_LIGHTS:
                // Same mapping as setLight
                if (index < 6) {
                    leftLights_[index + 3].brightness = brightness;
                } else if (index < 12) {
                    rightLights_[14 - index].brightness = brightness;
                } else if (index == 12) {
                    transmitLight_.brightness = brightness;
                }
                break;
            case LightIdentifier::GRIP_LIGHTS:
                // Same mapping as setLight
                if (index < 3) {
                    leftLights_[2 - index].brightness = brightness;
                } else if (index < 6) {
                    rightLights_[index - 3].brightness = brightness;
                }
                break;
            case LightIdentifier::GLOBAL:
                globalBrightness_ = brightness;
                break;
        }
    }

    void setGlobalBrightness(uint8_t brightness) override {
        globalBrightness_ = brightness;
    }

    LEDState::SingleLEDState getLight(LightIdentifier lightSet, uint8_t index) override {
        switch (lightSet) {
            case LightIdentifier::LEFT_LIGHTS:
                if (index < 9) return leftLights_[index];
                break;
            case LightIdentifier::RIGHT_LIGHTS:
                if (index < 9) return rightLights_[index];
                break;
            case LightIdentifier::TRANSMIT_LIGHT:
                return transmitLight_;
            default:
                break;
        }
        return LEDState::SingleLEDState();
    }

    void fade(LightIdentifier lightSet, uint8_t fadeAmount) override {
        auto fadeLED = [fadeAmount](LEDState::SingleLEDState& led) {
            led.color.red = (led.color.red > fadeAmount) ? led.color.red - fadeAmount : 0;
            led.color.green = (led.color.green > fadeAmount) ? led.color.green - fadeAmount : 0;
            led.color.blue = (led.color.blue > fadeAmount) ? led.color.blue - fadeAmount : 0;
        };

        switch (lightSet) {
            case LightIdentifier::LEFT_LIGHTS:
                for (auto& led : leftLights_) fadeLED(led);
                break;
            case LightIdentifier::RIGHT_LIGHTS:
                for (auto& led : rightLights_) fadeLED(led);
                break;
            case LightIdentifier::TRANSMIT_LIGHT:
                fadeLED(transmitLight_);
                break;
            case LightIdentifier::GLOBAL:
            case LightIdentifier::DISPLAY_LIGHTS:
                for (auto& led : leftLights_) fadeLED(led);
                for (auto& led : rightLights_) fadeLED(led);
                fadeLED(transmitLight_);
                break;
            default:
                break;
        }
    }

    void addToLight(LightIdentifier lightSet, uint8_t index, LEDState::SingleLEDState color) override {
        auto addColor = [](LEDState::SingleLEDState& dest, const LEDState::SingleLEDState& src) {
            dest.color.red = std::min(255, dest.color.red + src.color.red);
            dest.color.green = std::min(255, dest.color.green + src.color.green);
            dest.color.blue = std::min(255, dest.color.blue + src.color.blue);
            dest.brightness = std::max(dest.brightness, src.brightness);
        };

        switch (lightSet) {
            case LightIdentifier::LEFT_LIGHTS:
                if (index < 9) addColor(leftLights_[index], color);
                break;
            case LightIdentifier::RIGHT_LIGHTS:
                if (index < 9) addColor(rightLights_[index], color);
                break;
            case LightIdentifier::TRANSMIT_LIGHT:
                addColor(transmitLight_, color);
                break;
            default:
                break;
        }
    }

    void setFPS(uint8_t fps) override {
        fps_ = fps;
    }

    uint8_t getFPS() const override {
        return fps_;
    }

    // Dashboard access methods
    const std::array<LEDState::SingleLEDState, 9>& getLeftLights() const { return leftLights_; }
    const std::array<LEDState::SingleLEDState, 9>& getRightLights() const { return rightLights_; }
    const LEDState::SingleLEDState& getTransmitLight() const { return transmitLight_; }
    uint8_t getGlobalBrightness() const { return globalBrightness_; }

    void clear() {
        for (auto& led : leftLights_) led = LEDState::SingleLEDState();
        for (auto& led : rightLights_) led = LEDState::SingleLEDState();
        transmitLight_ = LEDState::SingleLEDState();
    }

private:
    std::array<LEDState::SingleLEDState, 9> leftLights_;
    std::array<LEDState::SingleLEDState, 9> rightLights_;
    LEDState::SingleLEDState transmitLight_;
    uint8_t globalBrightness_ = 255;
    uint8_t fps_ = 60;
};
