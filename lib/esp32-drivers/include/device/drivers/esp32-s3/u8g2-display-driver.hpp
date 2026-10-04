#pragma once

#include "device/drivers/driver-interface.hpp"
#include "device/drivers/esp32-s3/u8g2-esp32-spi-hal.hpp"

/**
 * A 128x64 monochrome panel driven through u8g2 over 4-wire SPI.
 *
 * The SSD1306 and SSD1309 subclasses below differ only in which u8g2 setup
 * function describes the panel, so the drawing lives here once. Calls are the
 * C API rather than the U8G2 C++ class: upstream u8g2 registers only csrc/ as
 * an ESP-IDF component, and its C++ wrapper binds to Arduino HAL symbols.
 */
class U8g2DisplayDriver : public DisplayDriverInterface {
public:
    using SetupFunction = void (*)(u8g2_t*, const u8g2_cb_t*, u8x8_msg_cb, u8x8_msg_cb);

    /** Records the panel and its pins; the bus is opened in initialize(). */
    U8g2DisplayDriver(const std::string& name, SetupFunction setup,
                      uint8_t csPin, uint8_t dcPin, uint8_t rstPin)
        : DisplayDriverInterface(name), setup(setup) {
        context.pins.sclk = static_cast<gpio_num_t>(DISPLAY_SCLK_PIN);
        context.pins.mosi = static_cast<gpio_num_t>(DISPLAY_MOSI_PIN);
        context.pins.cs = static_cast<gpio_num_t>(csPin);
        context.pins.dc = static_cast<gpio_num_t>(dcPin);
        context.pins.reset = static_cast<gpio_num_t>(rstPin);
        context.host = DISPLAY_SPI_HOST;
        context.device = nullptr;
        context.dmaScratch = nullptr;
    }

    ~U8g2DisplayDriver() override = default;

    int initialize() override {
        setup(&screen, U8G2_R0, u8g2Esp32SpiByteCallback, u8g2Esp32GpioAndDelayCallback);
        u8g2_SetUserPtr(&screen, &context);
        u8g2_InitDisplay(&screen);
        u8g2_SetPowerSave(&screen, 0);  // begin() left the panel on
        u8g2_ClearBuffer(&screen);
        u8g2_SetContrast(&screen, DEFAULT_CONTRAST);
        u8g2_SetFont(&screen, u8g2_font_tenfatguys_tf);
        u8g2_SetFontMode(&screen, 1);

        return 0;
    }

    void exec() override {
        // Display updates happen on render(), no periodic execution needed
    }

    Display* invalidateScreen() override {
        u8g2_ClearBuffer(&screen);
        return this;
    }

    void render() override {
        u8g2_SendBuffer(&screen);
    }

    Display* drawText(const char* text) override {
        u8g2_DrawStr(&screen, 0, 8, text);
        return this;
    }

    Display* drawText(const char* text, int xStart, int yStart) override {
        u8g2_DrawStr(&screen, xStart, yStart, text);
        return this;
    }

    Display* drawImage(Image image, int xStart, int yStart) override {
        int x = image.defaultStartX;
        int y = image.defaultStartY;

        if (xStart != -1) {
            x = xStart;
        }
        if (yStart != -1) {
            y = yStart;
        }

        u8g2_DrawXBMP(&screen, x, y, image.width, image.height, image.rawImage);
        return this;
    }

    Display* drawImage(Image image) override {
        drawImage(image, -1, -1);
        return this;
    }

    Display* renderGlyph(const char* unicodeForGlyph, int x, int y) override {
        u8g2_DrawUTF8(&screen, x, y, unicodeForGlyph);
        return this;
    }

    Display* drawButton(const char* text, int xCenter, int yCenter) override {
        u8g2_DrawButtonUTF8(&screen, xCenter, yCenter,
                            U8G2_BTN_BW2 | U8G2_BTN_HCENTER | U8G2_BTN_INV, 0, 2, 2, text);
        return this;
    }

    int getTextWidth(const char* text) override {
        if (text == nullptr) return 0;
        return static_cast<int>(u8g2_GetUTF8Width(&screen, text));
    }

    int getWidth() override { return 128; }

    void reset() {
        u8g2_ClearBuffer(&screen);
        u8g2_ClearDisplay(&screen);
    }

    Display* setGlyphMode(FontMode mode) override {
        // The C API draws UTF8 through u8g2_DrawUTF8 regardless, so the
        // enable/disableUTF8Print() pairs this replaced are gone: they only
        // ever affected the C++ Print stream, which nothing here used.
        switch (mode) {
            case FontMode::TEXT:
                u8g2_SetFont(&screen, u8g2_font_tenfatguys_tr);
                u8g2_SetDrawColor(&screen, 1);
                u8g2_SetFontMode(&screen, 0);
                break;
            case FontMode::NUMBER_GLYPH:
                u8g2_SetFont(&screen, u8g2_font_twelvedings_t_all);
                u8g2_SetDrawColor(&screen, 1);
                u8g2_SetFontMode(&screen, 0);
                break;
            case FontMode::LOADING_GLYPH:
                u8g2_SetFont(&screen, u8g2_font_unifont_t_76);
                u8g2_SetDrawColor(&screen, 1);
                u8g2_SetFontMode(&screen, 0);
                break;
            case FontMode::TEXT_INVERTED_SMALL:
                u8g2_SetFont(&screen, u8g2_font_tenthinnerguys_t_all);
                u8g2_SetFontMode(&screen, 1);
                u8g2_SetDrawColor(&screen, 2);
                break;
            case FontMode::TEXT_INVERTED_LARGE:
                u8g2_SetFont(&screen, u8g2_font_tenfatguys_tr);
                u8g2_SetFontMode(&screen, 1);
                u8g2_SetDrawColor(&screen, 2);
                break;
            case FontMode::SYMBOL_GLYPH:
                u8g2_SetFont(&screen, u8g2_font_open_iconic_all_4x_t);
                u8g2_SetDrawColor(&screen, 2);
                u8g2_SetFontMode(&screen, 1);
                break;
        }
        return this;
    }

    Display* whiteScreen() override {
        u8g2_ClearBuffer(&screen);
        u8g2_SetFontMode(&screen, 0);
        u8g2_SetDrawColor(&screen, 1);
        u8g2_DrawBox(&screen, 0, 0, u8g2_GetDisplayWidth(&screen), u8g2_GetDisplayHeight(&screen));
        return this;
    }

    Display* whiteScreenLeftHalf() override {
        u8g2_SetFontMode(&screen, 0);
        u8g2_SetDrawColor(&screen, 1);
        u8g2_DrawBox(&screen, 0, 0, u8g2_GetDisplayWidth(&screen) / 2, u8g2_GetDisplayHeight(&screen));
        return this;
    }

    Display* whiteScreenRightHalf() override {
        u8g2_SetFontMode(&screen, 0);
        u8g2_SetDrawColor(&screen, 1);
        u8g2_DrawBox(&screen, u8g2_GetDisplayWidth(&screen) / 2, 0,
                     u8g2_GetDisplayWidth(&screen) / 2, u8g2_GetDisplayHeight(&screen));
        return this;
    }

private:
    // The Arduino _4W_HW_SPI constructors took only cs/dc/reset and picked up
    // the clock and data lines from the board variant's SPI defaults.
    static constexpr uint8_t DISPLAY_SCLK_PIN = 12;
    static constexpr uint8_t DISPLAY_MOSI_PIN = 11;
    static constexpr spi_host_device_t DISPLAY_SPI_HOST = SPI2_HOST;
    static constexpr uint8_t DEFAULT_CONTRAST = 175;

    SetupFunction setup;
    u8g2_t screen = {};
    U8g2Esp32SpiContext context = {};
};
