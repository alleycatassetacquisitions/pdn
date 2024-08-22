#include "../../include/device/display.hpp"

U8G2_SSD1306_128X64_NONAME_F_4W_HW_SPI Display::getScreen()
{
    return screen;
}

void Display::drawTextToDisplay(char *text, int xStart = -1, int yStart = -1)
{
    int x = 8;
    int y = 8;

    if(xStart != -1) 
    {
        x *= xStart;
    } 
    else 
    {
        x = screen.getCursorX();
    }
    
    if(yStart != -1) 
    {
        y *= yStart;
    } 
    else 
    {
        y = screen.getCursorY();
    }

    screen.setCursor(x, y);
    screen.print(text);
}

Display::Display(int displayCS, int displayDC, int displayRST) :
    screen(U8G2_R0, displayCS, displayDC, displayRST)
{
    screen.begin();
    screen.clearBuffer();
    screen.setContrast(125);
    screen.setFont(u8g2_font_prospero_nbp_tf);
}

void Display::sendBuffer() 
{
    screen.sendBuffer();
}

void Display::clearBuffer() 
{
    screen.clearBuffer();
}

void Display::reset() 
{
    screen.clearDisplay();
}
