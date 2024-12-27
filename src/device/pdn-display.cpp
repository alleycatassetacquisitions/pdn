#include "device/pdn-display.hpp"

Display* PDNDisplay::invalidateScreen() {
    screen.clearBuffer();

    return this;
}

void PDNDisplay::render() {
    screen.sendBuffer();
}

Display* PDNDisplay::drawText(const char *text, int xStart, int yStart) {
    const int x = 8;
    const int y = 8;

    if (xStart != -1) {
        cursor.x = xStart;
    }

    if (yStart != -1) {
        cursor.y = yStart;
    }

    while(*text != '\0') {
        if(cursor.x >= maxCharX) {
            cursor.y++;
            cursor.x = 0;
        } else if(cursor.y > maxCharY) {
            return this;
        }

        uint16_t utf16_char = *text;
        //ESP_LOGD("PDN", "drawGlyph(%i, %i, %u)", cursor.x * x, cursor.y * y, utf16_char);
        screen.drawGlyph(cursor.x * x, cursor.y * y, utf16_char);
        text++;
        cursor.x++;
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

Display *PDNDisplay::drawTextInvertedColor(const char *text, int xStart, int yStart)
{
    size_t text_len = strlen(text);
    screen.drawBox(xStart * kCharWidth, yStart * kCharHeight, text_len * kCharWidth,  kCharHeight / 2);
    screen.setColorIndex(0);
    screen.drawStr(xStart * kCharWidth, yStart * kCharHeight, text);
    screen.setColorIndex(1);

    return nullptr;
}
Display * PDNDisplay::drawText(const char *text) {
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
    pinMode(displayCS, OUTPUT);
    pinMode(displayDC, OUTPUT);
    digitalWrite(displayCS, 0);
    digitalWrite(displayDC, 0);

    screen.begin();
    screen.clearBuffer();
    screen.setContrast(125);
    //screen.setFont(u8g2_font_prospero_nbp_tf);
    screen.setFont(u8g2_font_victoriabold8_8r);
}
