#pragma once

#include "device/drivers/driver-interface.hpp"
#include <array>

class NativeLightStripDriver : public LightDriverInterface {
public:
    NativeLightStripDriver(std::string name) : LightDriverInterface(name) {
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

    void setLight(LightIdentifier lightSet, uint8_t index, LEDState::SingleLEDState color) override {
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
                if (index < 9) {
                    leftLights_[index] = color;
                    rightLights_[index] = color;
                }
                break;
            case LightIdentifier::GRIP_LIGHTS:
                // Grip lights map to specific indices - assume bottom 3
                if (index < 3) {
                    leftLights_[index] = color;
                    rightLights_[index] = color;
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
                if (index < 9) {
                    leftLights_[index].brightness = brightness;
                    rightLights_[index].brightness = brightness;
                }
                break;
            case LightIdentifier::GRIP_LIGHTS:
                if (index < 3) {
                    leftLights_[index].brightness = brightness;
                    rightLights_[index].brightness = brightness;
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
