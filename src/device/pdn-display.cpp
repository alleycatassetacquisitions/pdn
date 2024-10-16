#include "device/pdn-display.hpp"

Display* PDNDisplay::invalidateScreen() {
    screen.clearBuffer();

    return this;
}

void PDNDisplay::render() {
    screen.sendBuffer();
}

Display* PDNDisplay::drawText(char *text, int xStart, int yStart) {
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

    return this;
}

Display* PDNDisplay::drawImage(Image image, int xStart, int yStart) {
    int x = image.defaultStartX;
    int y = image.defaultStartY;

    if(xStart != -1) {
        x = xStart;
    }
    if (yStart != -1) {
        y = yStart;
    }

    screen.drawXBMP(x, y, image.width, image.height, image.rawImage);

    return this;
}

Display * PDNDisplay::drawText(char *text) {
    drawText(text, -1, -1);
    return this;
}

Display * PDNDisplay::drawImage(Image image) {
    drawImage(image, -1, -1);
    return this;
}

void PDNDisplay::reset() {
    screen.clearBuffer();
    screen.clearDisplay();
}

PDNDisplay::PDNDisplay(int displayCS, int displayDC, int displayRST) : screen(U8G2_R0, displayCS, displayDC, displayRST) {
    screen.begin();
    screen.clearBuffer();
    screen.setContrast(125);
    screen.setFont(u8g2_font_prospero_nbp_tf);
}
