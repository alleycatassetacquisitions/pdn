#pragma once

#include <cstdint>

#include <driver/rmt_tx.h>
#include <driver/rmt_encoder.h>
#include <esp_err.h>

#include "device/drivers/driver-interface.hpp"
#include "device/drivers/logger.hpp"
#include "utils/simple-timer.hpp"

/**
 * One WS2812B pixel and its fixed-point colour arithmetic.
 *
 * The rounding in these formulas is exact, not incidental: changing it shifts
 * every dimmed colour, so the animations need re-tuning on hardware if it does.
 */
struct LedPixel {
    uint8_t red = 0;
    uint8_t green = 0;
    uint8_t blue = 0;

    /** Scales all three channels. The +1 makes a scale of 255 an exact identity. */
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
 * Two WS2812B strips on IDF's RMT transmitter.
 *
 * rmt_transmit queues a descriptor and returns, so a frame costs the main loop
 * the encode and nothing else; the clock-out overlaps the next ~16ms of game
 * logic. led_strip would do the same work but ends its refresh in
 * rmt_tx_wait_all_done(-1), which blocks for the whole frame.
 *
 * No end-of-frame reset symbol is encoded. WS2812B latches on the line sitting
 * low, and eot_level leaves it there between frames, so the inter-frame gap the
 * frame timer already enforces is ~16ms against a 280us requirement.
 *
 * Pins are constructor arguments, so one class serves both strips.
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
        closeStrip(displayStrip);
        closeStrip(gripStrip);
        delete[] displayLights;
        delete[] gripLights;
    }

    int initialize() override {
        // Both strips are attempted and the frame timer is armed either way:
        // pushStrip skips a null handle, so one strip failing to open must not
        // leave the other dark by never starting the timer that drives exec().
        const bool displayOpen = openStrip(displayPin, numDisplayLights, displayStrip);
        const bool gripOpen = openStrip(gripPin, numGripLights, gripStrip);
        setFPS(DEFAULT_FPS);
        return (displayOpen && gripOpen) ? 0 : -1;
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
        // Nothing calls this.
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
    /** One strip: its RMT channel, its encoder, and the GRB bytes in flight. */
    struct RmtStrip {
        rmt_channel_handle_t channel = nullptr;
        rmt_encoder_handle_t encoder = nullptr;
        uint8_t* grb = nullptr;
        uint8_t count = 0;
        bool inFlight = false;
    };

    bool openStrip(uint8_t pin, uint8_t count, RmtStrip& strip) {
        rmt_tx_channel_config_t channelConfig = {};
        channelConfig.gpio_num = static_cast<gpio_num_t>(pin);
        channelConfig.clk_src = RMT_CLK_SRC_DEFAULT;
        channelConfig.resolution_hz = RMT_RESOLUTION_HZ;
        channelConfig.mem_block_symbols = RMT_MEM_BLOCK_SYMBOLS;
        channelConfig.trans_queue_depth = 1;
        if (rmt_new_tx_channel(&channelConfig, &strip.channel) != ESP_OK) {
            LOG_E(name.c_str(), "rmt_new_tx_channel failed on pin %u", pin);
            return false;
        }

        // WS2812B at 10MHz resolution: one tick is 100ns. T0H 0.3us / T0L 0.9us,
        // T1H 0.9us / T1L 0.3us, green before red on the wire, MSB first.
        rmt_bytes_encoder_config_t encoderConfig = {};
        encoderConfig.bit0 = {.duration0 = 3, .level0 = 1, .duration1 = 9, .level1 = 0};
        encoderConfig.bit1 = {.duration0 = 9, .level0 = 1, .duration1 = 3, .level1 = 0};
        encoderConfig.flags.msb_first = 1;
        if (rmt_new_bytes_encoder(&encoderConfig, &strip.encoder) != ESP_OK) {
            LOG_E(name.c_str(), "rmt_new_bytes_encoder failed on pin %u", pin);
            closeStrip(strip);
            return false;
        }

        if (rmt_enable(strip.channel) != ESP_OK) {
            LOG_E(name.c_str(), "rmt_enable failed on pin %u", pin);
            closeStrip(strip);
            return false;
        }

        strip.count = count;
        strip.grb = new uint8_t[static_cast<size_t>(count) * 3]();
        return true;
    }

    void closeStrip(RmtStrip& strip) {
        if (strip.channel != nullptr) {
            rmt_disable(strip.channel);
            rmt_del_channel(strip.channel);
            strip.channel = nullptr;
        }
        if (strip.encoder != nullptr) {
            rmt_del_encoder(strip.encoder);
            strip.encoder = nullptr;
        }
        delete[] strip.grb;
        strip.grb = nullptr;
    }

    void pushStrip(RmtStrip& strip, const LedPixel* lights, uint8_t count) {
        if (strip.channel == nullptr || strip.grb == nullptr) {
            return;
        }
        // The buffer handed to rmt_transmit must stay untouched until that frame
        // has clocked out. The frame timer leaves ~16ms against a ~2ms transmit,
        // so this has always completed; it is bounded rather than assumed.
        if (strip.inFlight) {
            if (rmt_tx_wait_all_done(strip.channel, TX_DRAIN_TIMEOUT_MS) != ESP_OK) {
                LOG_W(name.c_str(), "previous LED frame still in flight, skipping this one");
                return;
            }
            strip.inFlight = false;
        }

        for (uint8_t i = 0; i < count; i++) {
            LedPixel pixel = lights[i];
            // Scaled on the way out rather than into the buffer, so a brightness
            // change does not degrade the stored colours.
            if (globalBrightness != 255) {
                pixel.scale(globalBrightness);
            }
            uint8_t* out = &strip.grb[static_cast<size_t>(i) * 3];
            out[0] = pixel.green;
            out[1] = pixel.red;
            out[2] = pixel.blue;
        }

        rmt_transmit_config_t txConfig = {};
        txConfig.loop_count = 0;
        // Leaves the line low after the last bit, which is the latch WS2812B
        // wants and why no reset symbol is encoded.
        txConfig.flags.eot_level = 0;
        if (rmt_transmit(strip.channel, strip.encoder, strip.grb,
                         static_cast<size_t>(count) * 3, &txConfig) != ESP_OK) {
            LOG_E(name.c_str(), "rmt_transmit failed");
            return;
        }
        strip.inFlight = true;
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
    static constexpr size_t RMT_MEM_BLOCK_SYMBOLS = 64;
    static constexpr int TX_DRAIN_TIMEOUT_MS = 20;
    static constexpr uint8_t DEFAULT_FPS = 60;

    uint8_t displayPin;
    uint8_t gripPin;
    LedPixel* displayLights;
    LedPixel* gripLights;
    uint8_t numDisplayLights;
    uint8_t numGripLights;
    RmtStrip displayStrip;
    RmtStrip gripStrip;
    SimpleTimer frameTimer;
    uint8_t globalBrightness = 255;
    uint8_t fps = DEFAULT_FPS;
};
