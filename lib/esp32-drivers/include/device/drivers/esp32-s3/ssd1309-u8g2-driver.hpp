#pragma once

#include "device/drivers/driver-interface.hpp"
#include <U8g2lib.h>
#include <Arduino.h>

class SSD1309U8G2Driver : public DisplayDriverInterface {
public:
    explicit SSD1309U8G2Driver(const std::string& name, uint8_t csPin, uint8_t dcPin, uint8_t rstPin)
        : DisplayDriverInterface(name), screen(U8G2_R0, csPin, dcPin, rstPin),
          csPin(csPin), dcPin(dcPin) {
        pinMode(csPin, OUTPUT);
        pinMode(dcPin, OUTPUT);
        digitalWrite(csPin, 0);
        digitalWrite(dcPin, 0);
    }

    ~SSD1309U8G2Driver() override = default;

    int initialize() override {
        screen.begin();
        screen.clearBuffer();
        screen.setContrast(175);
        screen.setFont(u8g2_font_tenfatguys_tf);
        screen.setFontMode(1);

        return 0;
    }

    void exec() override {
        // Display updates happen on render(), no periodic execution needed
    }

    Display* invalidateScreen() override {
        screen.clearBuffer();
        return this;
    }

    void render() override {
        screen.sendBuffer();
    }

    Display* drawText(const char *text) override {
        screen.drawStr(0, 8, text);
        return this;
    }

    Display* drawText(const char *text, int xStart, int yStart) override {
        screen.drawStr(xStart, yStart, text);
        return this;
    }

    Display* drawImage(Image image, int xStart, int yStart) override {
        int x = image.defaultStartX;
        int y = image.defaultStartY;

        if(xStart != -1) {
            x = xStart;
        }
        if(yStart != -1) {
            y = yStart;
        }

        screen.drawXBMP(x, y, image.width, image.height, image.rawImage);
        return this;
    }

    Display* drawImage(Image image) override {
        drawImage(image, -1, -1);
        return this;
    }

    Display* renderGlyph(const char* unicodeForGlyph, int x, int y) override {
        screen.drawUTF8(x, y, unicodeForGlyph);
        return this;
    }

    Display* drawButton(const char *text, int xCenter, int yCenter) override {
        screen.drawButtonUTF8(xCenter, yCenter, U8G2_BTN_BW2 | U8G2_BTN_HCENTER | U8G2_BTN_INV, 0, 2, 2, text);
        return this;
    }

    int getTextWidth(const char* text) override {
        if (text == nullptr) return 0;
        return static_cast<int>(screen.getUTF8Width(text));
    }

    int getWidth() override { return 128; }

    void reset() {
        screen.clearBuffer();
        screen.clearDisplay();
    }

    Display* setGlyphMode(FontMode mode) override {
        switch (mode) {
            case FontMode::TEXT:
                screen.disableUTF8Print();
                screen.setFont(u8g2_font_tenfatguys_tr);
                screen.setDrawColor(1);
                screen.setFontMode(0);
                break;
            case FontMode::NUMBER_GLYPH:
                screen.enableUTF8Print();
                screen.setFont(u8g2_font_twelvedings_t_all);
                screen.setDrawColor(1);
                screen.setFontMode(0);
                break;
            case FontMode::LOADING_GLYPH:
                screen.enableUTF8Print();
                screen.setFont(u8g2_font_unifont_t_76);
                screen.setDrawColor(1);
                screen.setFontMode(0);
                break;
            case FontMode::TEXT_INVERTED_SMALL:
                screen.setFont(u8g2_font_tenthinnerguys_t_all);
                screen.setFontMode(1);
                screen.setDrawColor(2);
                break;
            case FontMode::TEXT_INVERTED_LARGE:
                screen.setFont(u8g2_font_tenfatguys_tr);
                screen.setFontMode(1);
                screen.setDrawColor(2);
                break;
            case FontMode::SYMBOL_GLYPH:
                screen.setFont(u8g2_font_open_iconic_all_4x_t);
                screen.setDrawColor(2);
                screen.setFontMode(1);
                break;
            case FontMode::ARCADE_1:
                screen.enableUTF8Print();
                screen.setFont(u8g2_font_cu12_h_symbols);
                screen.setFontMode(1);
                screen.setDrawColor(2);
                break;
        }
        return this;
    }

    Display* drawFilledCircle(int x, int y, int radius) override {
        if (radius < 1) {
            return this;
        }
        screen.setDrawColor(1);
        screen.drawDisc(x, y, radius, U8G2_DRAW_ALL);
        return this;
    }

    Display* whiteScreen() override {
        screen.clearBuffer();
        screen.setFontMode(0);
        screen.setDrawColor(1);
        screen.drawBox(0, 0, screen.getDisplayWidth(), screen.getDisplayHeight());
        return this;
    }

    Display* whiteScreenLeftHalf() override {
        screen.setFontMode(0);
        screen.setDrawColor(1);
        screen.drawBox(0, 0, screen.getDisplayWidth() / 2, screen.getDisplayHeight());
        return this;
    }

    Display* whiteScreenRightHalf() override {
        screen.setFontMode(0);
        screen.setDrawColor(1);
        screen.drawBox(screen.getDisplayWidth() / 2, 0, screen.getDisplayWidth() / 2, screen.getDisplayHeight());
        return this;
    }

private:
    // NONAME0 vs NONAME2: u8g2's SSD1309 128x64 "NONAME2" build adds a +2 column address offset when
    // blitting to the panel (u8x8: default_x_offset=2) for 132-px-wide internal RAM. Use NONAME0 when
    // the FDN panel maps buffer column 0 to the left edge; NONAME2 otherwise—wrong choice shows a
    // ghost/fixed column and wrong half-screen split.
    U8G2_SSD1309_128X64_NONAME0_F_4W_HW_SPI screen;
    uint8_t csPin;
    uint8_t dcPin;
};
