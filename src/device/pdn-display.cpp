#include "device/pdn-display.hpp"

Display* PDNDisplay::invalidateScreen() {
    screen.clearBuffer();

    return this;
}

void PDNDisplay::render() {
    screen.sendBuffer();
}

Display* PDNDisplay::drawText(const char *text, int xStart, int yStart) {
    screen.drawStr(xStart, yStart, text);
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
    drawText(text, 0, 8);
    return this;
}

Display * PDNDisplay::drawImage(Image image) {
    drawImage(image, -1, -1);
    return this;
}

Display * PDNDisplay::setGlyphMode(FontMode mode) {
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

Display * PDNDisplay::drawButton(const char *text, int xCenter, int yCenter) {
    screen.drawButtonUTF8(xCenter, yCenter, U8G2_BTN_BW2 | U8G2_BTN_HCENTER | U8G2_BTN_INV, 0, 2, 2, text);
    return this;
}

Display * PDNDisplay::renderGlyph(const char* unicodeForGlyph, int xStart, int yStart) {
    screen.drawUTF8(xStart, yStart, unicodeForGlyph);
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
    screen.setContrast(175);
    screen.setFont(u8g2_font_tenfatguys_tf);
    screen.setFontMode(1);
}
