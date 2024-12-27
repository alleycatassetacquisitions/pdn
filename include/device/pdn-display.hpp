#pragma once

#include <U8g2lib.h>

#include "display.hpp"
#include "image.hpp"

class PDNDisplay : public Display {
public:
    PDNDisplay(int displayCS, int displayDC, int displayRST);

    Display* invalidateScreen() override;

    void render() override;

    Display* drawText(const char *text) override;

    Display* drawImage(Image image) override;

    Display* setGlyphMode(FontMode mode) override;

    Display* renderGlyph(const char* unicodeForGlyph, int xStart, int yStart) override;

    Display* drawText(const char *text, int xStart, int yStart) override;

    Display* drawImage(Image image, int xStart, int yStart) override;

    Display* drawButton(const char *text, int xCenter, int yCenter) override;

    void reset();

    std::tuple<int,int> getSizeInPixels()
    {
        return {128,64};
    }

    std::tuple<int,int> getSizeInChar()
    {
        return {128/8, 64 / 8};
    }

    const int maxCharX = 128 / 8;
    const int maxCharY = 64 / 8;

private:
    U8G2_SSD1306_128X64_NONAME_F_4W_HW_SPI screen;
};
