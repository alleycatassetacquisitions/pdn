#include "../../include/device/display.hpp"

U8G2_SSD1306_128X64_NONAME_F_4W_HW_SPI Display::getScreen() {
    return screen;
}

void Display::drawText(char *text, int xStart, int yStart) {
    int x = 8;
    int y = 8;

    if (xStart != -1) {
        cursor.x = x*xStart;
    }

    if (yStart != -1) {
        cursor.y = y*yStart;
    }

    while(*text != '\0') {
        if(cursor.x >= maxCharX) {
            cursor.y += y;
            cursor.x = 0;
        } else if(cursor.y > maxCharY) {
            cursor.x = 0;
            cursor.y = y;
        }

        screen.drawStr(cursor.x, cursor.y, text);
        text++;
        cursor.x += x;
    }
}

void Display::drawImage(Image image, int xStart, int yStart) {
    int x = image.defaultStartX;
    int y = image.defaultStartY;

    if(xStart != -1) {
        x = xStart;
    }
    if (yStart != -1) {
        y = yStart;
    }

    screen.drawXBMP(x, y, image.width, image.height, image.rawImage);
}

Display::Display(int displayCS, int displayDC, int displayRST) : screen(U8G2_R0, displayCS, displayDC, displayRST) {
    screen.begin();
    screen.clearBuffer();
    screen.setContrast(125);
    screen.setFont(u8g2_font_prospero_nbp_tf);
}

void Display::sendBuffer() {
    screen.sendBuffer();
}

void Display::clearBuffer() {
    screen.clearBuffer();
}

void Display::reset() {
    screen.clearDisplay();
}
