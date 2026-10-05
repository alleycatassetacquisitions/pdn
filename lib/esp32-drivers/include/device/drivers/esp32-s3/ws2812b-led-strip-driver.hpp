#pragma once

#include <cstdint>

#include <led_strip.h>

#include "device/drivers/driver-interface.hpp"
#include "device/drivers/logger.hpp"
#include "utils/simple-timer.hpp"

/**
 * One WS2812B pixel, carrying FastLED's colour arithmetic rather than an
 * approximation of it.
 *
 * Each is the formula FastLED compiled with FASTLED_SCALE8_FIXED = 1, its
 * default. Rounding differently shifts every dimmed colour, so re-tune the
 * animations on hardware if these change.
 */
struct LedPixel {
    uint8_t red = 0;
    uint8_t green = 0;
    uint8_t blue = 0;

    /** FastLED's nscale8x3: the +1 makes a scale of 255 an exact identity. */
    void scale(uint8_t amount) {
        uint16_t fixed = static_cast<uint16_t>(amount) + 1;
        red = static_cast<uint8_t>((static_cast<uint16_t>(red) * fixed) >> 8);
        green = static_cast<uint8_t>((static_cast<uint16_t>(green) * fixed) >> 8);
        blue = static_cast<uint8_t>((static_cast<uint16_t>(blue) * fixed) >> 8);
    }

    /** CRGB::operator+= saturates per channel; it does not wrap. */
    void add(const LedPixel& other) {
        red = saturatingAdd(red, other.red);
        green = saturatingAdd(green, other.green);
        blue = saturatingAdd(blue, other.blue);
    }

private:
    static uint8_t saturatingAdd(uint8_t left, uint8_t right) {
        uint16_t sum = static_cast<uint16_t>(left) + static_cast<uint16_t>(right);
        return sum > 255 ? 255 : static_cast<uint8_t>(sum);
    }
};

/**
 * Two WS2812B strips over RMT.
 *
 * Replaces FastLED, which owned both the wire protocol and the colour maths.
 * The protocol is now led_strip's; the maths is LedPixel's above. Pins are
 * constructor arguments rather than template parameters, which FastLED needed
 * them to be.
 */
class WS2812BLedStripDriver : public LightDriverInterface {
public:
    /** Allocates both buffers; the RMT channels are opened in initialize(). */
    WS2812BLedStripDriver(const std::string& name, uint8_t displayPin, uint8_t gripPin,
                          uint8_t numDisplayLights, uint8_t numGripLights)
        : LightDriverInterface(name),
          displayPin(displayPin),
          gripPin(gripPin),
          numDisplayLights(numDisplayLights),
          numGripLights(numGripLights) {
        displayLights = new LedPixel[numDisplayLights];
        gripLights = new LedPixel[numGripLights];
    }

    ~WS2812BLedStripDriver() override {
        delete[] displayLights;
        delete[] gripLights;
        if (displayStrip != nullptr) {
            led_strip_del(displayStrip);
        }
        if (gripStrip != nullptr) {
            led_strip_del(gripStrip);
        }
    }

    int initialize() override {
        if (!openStrip(displayPin, numDisplayLights, &displayStrip)) {
            return -1;
        }
        if (!openStrip(gripPin, numGripLights, &gripStrip)) {
            return -1;
        }
        setFPS(DEFAULT_FPS);
        return 0;
    }

    void exec() override {
        if (frameTimer.expired()) {
            frameTimer.setTimer(1000 / fps);
            pushStrip(displayStrip, displayLights, numDisplayLights);
            pushStrip(gripStrip, gripLights, numGripLights);
        }
    }

    void setLight(LightIdentifier lightSet, uint8_t index, LEDState::SingleLEDState color) override {
        LedPixel* pixel = pixelAt(lightSet, index);
        if (pixel == nullptr) {
            return;
        }
        *pixel = LedPixel{color.color.red, color.color.green, color.color.blue};
        pixel->scale(color.brightness);
    }

    /** Scales one pixel's colour by `brightness`, in place. Nothing in the game
     * calls this; `setLight` carries all production traffic. */
    void setLightBrightness(LightIdentifier lightSet, uint8_t index, uint8_t brightness) override {
        LedPixel* pixel = pixelAt(lightSet, index);
        if (pixel != nullptr) {
            pixel->scale(brightness);
        }
    }

    void setGlobalBrightness(uint8_t brightness) override {
        globalBrightness = brightness;
    }

    LEDState::SingleLEDState getLight(LightIdentifier lightSet, uint8_t index) override {
        LedPixel* pixel = pixelAt(lightSet, index);
        if (pixel == nullptr) {
            return LEDState::SingleLEDState();
        }
        // Nothing calls this. The FastLED version dropped blue, because
        // LEDColor's parameters default and it passed only two, so the real
        // blue landed in brightness instead.
        return LEDState::SingleLEDState(LEDColor(pixel->red, pixel->green, pixel->blue), 255);
    }

    void fade(LightIdentifier lightSet, uint8_t fadeAmount) override {
        LedPixel* lights = bufferFor(lightSet);
        if (lights == nullptr) {
            return;
        }
        // fadeToBlackBy(n, amount) is nscale8(n, 255 - amount).
        uint8_t remaining = static_cast<uint8_t>(255 - fadeAmount);
        for (uint8_t i = 0; i < countFor(lightSet); i++) {
            lights[i].scale(remaining);
        }
    }

    void addToLight(LightIdentifier lightSet, uint8_t index, LEDState::SingleLEDState color) override {
        LedPixel* pixel = pixelAt(lightSet, index);
        if (pixel == nullptr) {
            return;
        }
        pixel->add(LedPixel{color.color.red, color.color.green, color.color.blue});
        pixel->scale(color.brightness);
    }

    void setFPS(uint8_t fps) override {
        this->fps = fps;
        frameTimer.setTimer(1000 / fps);
    }

    uint8_t getFPS() const override {
        return fps;
    }

private:
    bool openStrip(uint8_t pin, uint8_t count, led_strip_handle_t* out) {
        led_strip_config_t strip = {};
        strip.strip_gpio_num = pin;
        strip.max_leds = count;
        strip.led_model = LED_MODEL_WS2812;
        // FastLED's addLeds<WS2812B, PIN, GRB> declared the wire order.
        strip.color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB;

        led_strip_rmt_config_t rmt = {};
        rmt.resolution_hz = RMT_RESOLUTION_HZ;

        if (led_strip_new_rmt_device(&strip, &rmt, out) != ESP_OK) {
            LOG_E(name.c_str(), "led_strip_new_rmt_device failed on pin %u", pin);
            return false;
        }
        return true;
    }

    void pushStrip(led_strip_handle_t strip, const LedPixel* lights, uint8_t count) {
        if (strip == nullptr) {
            return;
        }
        for (uint8_t i = 0; i < count; i++) {
            LedPixel pixel = lights[i];
            // FastLED applied its global brightness at show() rather than into
            // the buffer, so the same scaling happens here on the way out. It
            // is an identity in practice: nothing calls setGlobalBrightness and
            // FastLED's own default was 255. Losing FastLED's binary dithering
            // costs nothing for a different reason: show() disabled it on every
            // frame below 100 FPS and this ran at 60.
            if (globalBrightness != 255) {
                pixel.scale(globalBrightness);
            }
            led_strip_set_pixel(strip, i, pixel.red, pixel.green, pixel.blue);
        }
        led_strip_refresh(strip);
    }

    LedPixel* bufferFor(LightIdentifier lightSet) {
        if (lightSet == LightIdentifier::DISPLAY_LIGHTS) return displayLights;
        if (lightSet == LightIdentifier::GRIP_LIGHTS) return gripLights;
        return nullptr;
    }

    uint8_t countFor(LightIdentifier lightSet) const {
        if (lightSet == LightIdentifier::DISPLAY_LIGHTS) return numDisplayLights;
        if (lightSet == LightIdentifier::GRIP_LIGHTS) return numGripLights;
        return 0;
    }

    LedPixel* pixelAt(LightIdentifier lightSet, uint8_t index) {
        LedPixel* buffer = bufferFor(lightSet);
        if (buffer == nullptr || index >= countFor(lightSet)) {
            return nullptr;
        }
        return &buffer[index];
    }

    static constexpr uint32_t RMT_RESOLUTION_HZ = 10000000;
    static constexpr uint8_t DEFAULT_FPS = 60;

    uint8_t displayPin;
    uint8_t gripPin;
    LedPixel* displayLights;
    LedPixel* gripLights;
    uint8_t numDisplayLights;
    uint8_t numGripLights;
    led_strip_handle_t displayStrip = nullptr;
    led_strip_handle_t gripStrip = nullptr;
    SimpleTimer frameTimer;
    uint8_t globalBrightness = 255;
    uint8_t fps = DEFAULT_FPS;
};
