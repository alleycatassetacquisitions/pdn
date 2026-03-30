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
            case LightIdentifier::RECESS_LIGHTS:
                if (index < 23) recessLights_[index] = color;
                break;
            case LightIdentifier::FIN_LIGHTS:
                if (index < 9) finLights_[index] = color;
                break;
            case LightIdentifier::GLOBAL:
                for (auto& led : recessLights_) led = color;
                for (auto& led : finLights_) led = color;
                break;
        }
    }

    void setLightBrightness(LightIdentifier lightSet, uint8_t index, uint8_t brightness) override {
        switch (lightSet) {
            case LightIdentifier::RECESS_LIGHTS:
                if (index < 23) recessLights_[index].brightness = brightness;
                break;
            case LightIdentifier::FIN_LIGHTS:
                if (index < 9) finLights_[index].brightness = brightness;
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
            case LightIdentifier::RECESS_LIGHTS:
                if (index < 23) return recessLights_[index];
                break;
            case LightIdentifier::FIN_LIGHTS:
                if (index < 9) return finLights_[index];
                break;
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
            case LightIdentifier::RECESS_LIGHTS:
                for (auto& led : recessLights_) fadeLED(led);
                break;
            case LightIdentifier::FIN_LIGHTS:
                for (auto& led : finLights_) fadeLED(led);
                break;
            case LightIdentifier::GLOBAL:
                for (auto& led : recessLights_) fadeLED(led);
                for (auto& led : finLights_) fadeLED(led);
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
            case LightIdentifier::RECESS_LIGHTS:
                if (index < 23) addColor(recessLights_[index], color);
                break;
            case LightIdentifier::FIN_LIGHTS:
                if (index < 9) addColor(finLights_[index], color);
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
    const std::array<LEDState::SingleLEDState, 23>& getRecessLights() const { return recessLights_; }
    const std::array<LEDState::SingleLEDState, 9>& getFinLights() const { return finLights_; }
    uint8_t getGlobalBrightness() const { return globalBrightness_; }

    void clear() {
        for (auto& led : recessLights_) led = LEDState::SingleLEDState();
        for (auto& led : finLights_) led = LEDState::SingleLEDState();
    }

private:
    std::array<LEDState::SingleLEDState, 23> recessLights_;
    std::array<LEDState::SingleLEDState, 9> finLights_;
    uint8_t globalBrightness_ = 255;
    uint8_t fps_ = 60;
};
