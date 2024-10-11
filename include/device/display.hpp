#pragma once

#include <U8g2lib.h>

#include "../../lib/pdn-libs/image.hpp"

struct Cursor {
    int x = 0;
    int y = 0;
};

class Display {
public:
    Display(int displayCS, int displayDC, int displayRST);

    U8G2_SSD1306_128X64_NONAME_F_4W_HW_SPI getScreen();

    void drawText(char *text, int xStart = -1, int yStart = -1);

    void drawImage(Image image, int xStart = -1, int yStart = -1);

    void reset();

    const int maxCharX = 128 / 8;
    const int maxCharY = 64 / 8;

    void sendBuffer();

    void clearBuffer();

private:
    Cursor cursor;
    U8G2_SSD1306_128X64_NONAME_F_4W_HW_SPI screen;
};
