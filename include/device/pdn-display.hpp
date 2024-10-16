#pragma once

#include <U8g2lib.h>

#include "display.hpp"
#include "image.hpp"

class PDNDisplay : public Display {
public:
    PDNDisplay(int displayCS, int displayDC, int displayRST);

    Display* invalidateScreen() override;

    void render() override;

    Display* drawText(char *text) override;

    Display* drawImage(Image image) override;

    Display* drawText(char *text, int xStart, int yStart) override;

    Display* drawImage(Image image, int xStart, int yStart) override;

    void reset();

    const int maxCharX = 128 / 8;
    const int maxCharY = 64 / 8;

private:
    U8G2_SSD1306_128X64_NONAME_F_4W_HW_SPI screen;
};
