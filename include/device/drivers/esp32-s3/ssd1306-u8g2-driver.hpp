#pragma once

#include "device/drivers/driver-interface.hpp"
#include <U8g2lib.h>
#include <Arduino.h>
#include "device/device-constants.hpp"


class SSD1306U8G2Driver : public DisplayDriverInterface {
public:
    explicit SSD1306U8G2Driver(const std::string& name) 
        : DisplayDriverInterface(name), screen(U8G2_R0, displayCS, displayDC, displayRST) {
        pinMode(displayCS, OUTPUT);
        pinMode(displayDC, OUTPUT);
        digitalWrite(displayCS, 0);
        digitalWrite(displayDC, 0);
    }

    ~SSD1306U8G2Driver() override = default;

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
        }
        return this;
    }

    Display* drawBox(int x, int y, int w, int h) override {
        screen.drawBox(x, y, w, h);
        return this;
    }

    Display* drawFrame(int x, int y, int w, int h) override {
        screen.drawFrame(x, y, w, h);
        return this;
    }

    Display* drawGlyph(int x, int y, uint16_t glyph) override {
        screen.drawGlyph(x, y, glyph);
        return this;
    }

    Display* setDrawColor(int color) override {
        screen.setDrawColor(color);
        return this;
    }

    Display* setFont(const uint8_t *font) override {
        screen.setFont(font);
        return this;
    }

private:
    U8G2_SSD1306_128X64_NONAME_F_4W_HW_SPI screen;
};