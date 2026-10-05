//
// Created by Elli Furedy on 10/12/2024.
//

#pragma once
#include "device/drivers/driver-interface.hpp"
#include "device/drivers/logger.hpp"

#include <driver/ledc.h>

class Esp32S3HapticsDriver : public HapticsMotorDriverInterface {
public:
    Esp32S3HapticsDriver(const std::string& name, int pin) 
        : HapticsMotorDriverInterface(name), pinNumber(pin), intensity(0), active(false) {
    }

    ~Esp32S3HapticsDriver() override {
        setDuty(0);
    }

    int initialize() override {
        // 8-bit at 1kHz. These two are a calibration against the physical motor,
        // not an arbitrary choice: changing either changes how every haptic cue
        // feels, so re-tune on hardware rather than on a spec sheet.
        ledc_timer_config_t timer = {};
        timer.speed_mode = LEDC_LOW_SPEED_MODE;
        timer.duty_resolution = LEDC_TIMER_8_BIT;
        timer.timer_num = HAPTICS_TIMER;
        timer.freq_hz = PWM_FREQUENCY_HZ;
        timer.clk_cfg = LEDC_AUTO_CLK;
        if (ledc_timer_config(&timer) != ESP_OK) {
            LOG_E(name.c_str(), "haptics LEDC timer config failed");
            return -1;
        }

        ledc_channel_config_t channel = {};
        channel.gpio_num = pinNumber;
        channel.speed_mode = LEDC_LOW_SPEED_MODE;
        channel.channel = HAPTICS_CHANNEL;
        channel.timer_sel = HAPTICS_TIMER;
        channel.duty = 0;
        channel.hpoint = 0;
        if (ledc_channel_config(&channel) != ESP_OK) {
            LOG_E(name.c_str(), "haptics LEDC channel config failed");
            return -1;
        }
        return 0;
    }

    void exec() override {
        // No periodic execution needed for haptics driver
    }

    bool isOn() override {
        return active;
    }

    void max() override {
        intensity = 255;
        active = true;
        setDuty(255);
    }

    void setIntensity(int value) override {
        if (value > 255) intensity = 255;
        else if (value < 0) intensity = 0;
        else intensity = value;

        setDuty(intensity);
    }

    int getIntensity() override {
        return intensity;
    }

    void off() override {
        intensity = 0;
        active = false;
        setDuty(0);
    }

private:
    void setDuty(int duty) {
        ledc_set_duty(LEDC_LOW_SPEED_MODE, HAPTICS_CHANNEL, duty);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, HAPTICS_CHANNEL);
    }

    // Nothing else in the tree drives LEDC, so timer 0 and channel 0 are free.
    static constexpr ledc_timer_t HAPTICS_TIMER = LEDC_TIMER_0;
    static constexpr ledc_channel_t HAPTICS_CHANNEL = LEDC_CHANNEL_0;
    static constexpr uint32_t PWM_FREQUENCY_HZ = 1000;

    int pinNumber;
    int intensity;
    bool active;
};
