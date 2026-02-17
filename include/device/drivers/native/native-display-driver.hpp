#pragma once

#include "device/drivers/driver-interface.hpp"
#include <string>
#include <deque>
#include <cstring>
#include <vector>

// Simple 5x7 bitmap font for basic ASCII characters (space through ~)
// Each character is 5 pixels wide, 7 pixels tall
static const uint8_t FONT_5X7[][5] = {
    {0x00,0x00,0x00,0x00,0x00}, // space
    {0x00,0x00,0x5F,0x00,0x00}, // !
    {0x00,0x07,0x00,0x07,0x00}, // "
    {0x14,0x7F,0x14,0x7F,0x14}, // #
    {0x24,0x2A,0x7F,0x2A,0x12}, // $
    {0x23,0x13,0x08,0x64,0x62}, // %
    {0x36,0x49,0x55,0x22,0x50}, // &
    {0x00,0x05,0x03,0x00,0x00}, // '
    {0x00,0x1C,0x22,0x41,0x00}, // (
    {0x00,0x41,0x22,0x1C,0x00}, // )
    {0x08,0x2A,0x1C,0x2A,0x08}, // *
    {0x08,0x08,0x3E,0x08,0x08}, // +
    {0x00,0x50,0x30,0x00,0x00}, // ,
    {0x08,0x08,0x08,0x08,0x08}, // -
    {0x00,0x60,0x60,0x00,0x00}, // .
    {0x20,0x10,0x08,0x04,0x02}, // /
    {0x3E,0x51,0x49,0x45,0x3E}, // 0
    {0x00,0x42,0x7F,0x40,0x00}, // 1
    {0x42,0x61,0x51,0x49,0x46}, // 2
    {0x21,0x41,0x45,0x4B,0x31}, // 3
    {0x18,0x14,0x12,0x7F,0x10}, // 4
    {0x27,0x45,0x45,0x45,0x39}, // 5
    {0x3C,0x4A,0x49,0x49,0x30}, // 6
    {0x01,0x71,0x09,0x05,0x03}, // 7
    {0x36,0x49,0x49,0x49,0x36}, // 8
    {0x06,0x49,0x49,0x29,0x1E}, // 9
    {0x00,0x36,0x36,0x00,0x00}, // :
    {0x00,0x56,0x36,0x00,0x00}, // ;
    {0x00,0x08,0x14,0x22,0x41}, // <
    {0x14,0x14,0x14,0x14,0x14}, // =
    {0x41,0x22,0x14,0x08,0x00}, // >
    {0x02,0x01,0x51,0x09,0x06}, // ?
    {0x32,0x49,0x79,0x41,0x3E}, // @
    {0x7E,0x11,0x11,0x11,0x7E}, // A
    {0x7F,0x49,0x49,0x49,0x36}, // B
    {0x3E,0x41,0x41,0x41,0x22}, // C
    {0x7F,0x41,0x41,0x22,0x1C}, // D
    {0x7F,0x49,0x49,0x49,0x41}, // E
    {0x7F,0x09,0x09,0x01,0x01}, // F
    {0x3E,0x41,0x41,0x51,0x32}, // G
    {0x7F,0x08,0x08,0x08,0x7F}, // H
    {0x00,0x41,0x7F,0x41,0x00}, // I
    {0x20,0x40,0x41,0x3F,0x01}, // J
    {0x7F,0x08,0x14,0x22,0x41}, // K
    {0x7F,0x40,0x40,0x40,0x40}, // L
    {0x7F,0x02,0x04,0x02,0x7F}, // M
    {0x7F,0x04,0x08,0x10,0x7F}, // N
    {0x3E,0x41,0x41,0x41,0x3E}, // O
    {0x7F,0x09,0x09,0x09,0x06}, // P
    {0x3E,0x41,0x51,0x21,0x5E}, // Q
    {0x7F,0x09,0x19,0x29,0x46}, // R
    {0x46,0x49,0x49,0x49,0x31}, // S
    {0x01,0x01,0x7F,0x01,0x01}, // T
    {0x3F,0x40,0x40,0x40,0x3F}, // U
    {0x1F,0x20,0x40,0x20,0x1F}, // V
    {0x7F,0x20,0x18,0x20,0x7F}, // W
    {0x63,0x14,0x08,0x14,0x63}, // X
    {0x03,0x04,0x78,0x04,0x03}, // Y
    {0x61,0x51,0x49,0x45,0x43}, // Z
    {0x00,0x00,0x7F,0x41,0x41}, // [
    {0x02,0x04,0x08,0x10,0x20}, // backslash
    {0x41,0x41,0x7F,0x00,0x00}, // ]
    {0x04,0x02,0x01,0x02,0x04}, // ^
    {0x40,0x40,0x40,0x40,0x40}, // _
    {0x00,0x01,0x02,0x04,0x00}, // `
    {0x20,0x54,0x54,0x54,0x78}, // a
    {0x7F,0x48,0x44,0x44,0x38}, // b
    {0x38,0x44,0x44,0x44,0x20}, // c
    {0x38,0x44,0x44,0x48,0x7F}, // d
    {0x38,0x54,0x54,0x54,0x18}, // e
    {0x08,0x7E,0x09,0x01,0x02}, // f
    {0x08,0x14,0x54,0x54,0x3C}, // g
    {0x7F,0x08,0x04,0x04,0x78}, // h
    {0x00,0x44,0x7D,0x40,0x00}, // i
    {0x20,0x40,0x44,0x3D,0x00}, // j
    {0x00,0x7F,0x10,0x28,0x44}, // k
    {0x00,0x41,0x7F,0x40,0x00}, // l
    {0x7C,0x04,0x18,0x04,0x78}, // m
    {0x7C,0x08,0x04,0x04,0x78}, // n
    {0x38,0x44,0x44,0x44,0x38}, // o
    {0x7C,0x14,0x14,0x14,0x08}, // p
    {0x08,0x14,0x14,0x18,0x7C}, // q
    {0x7C,0x08,0x04,0x04,0x08}, // r
    {0x48,0x54,0x54,0x54,0x20}, // s
    {0x04,0x3F,0x44,0x40,0x20}, // t
    {0x3C,0x40,0x40,0x20,0x7C}, // u
    {0x1C,0x20,0x40,0x20,0x1C}, // v
    {0x3C,0x40,0x30,0x40,0x3C}, // w
    {0x44,0x28,0x10,0x28,0x44}, // x
    {0x0C,0x50,0x50,0x50,0x3C}, // y
    {0x44,0x64,0x54,0x4C,0x44}, // z
    {0x00,0x08,0x36,0x41,0x00}, // {
    {0x00,0x00,0x7F,0x00,0x00}, // |
    {0x00,0x41,0x36,0x08,0x00}, // }
    {0x08,0x08,0x2A,0x1C,0x08}, // ~
};

class NativeDisplayDriver : public DisplayDriverInterface {
public:
    static const int WIDTH = 128;
    static const int HEIGHT = 64;
    
    explicit NativeDisplayDriver(const std::string& name) : DisplayDriverInterface(name) {
        clearBuffer();
    }

    ~NativeDisplayDriver() override = default;

    int initialize() override {
        return 0;
    }

    void exec() override {
        // No periodic execution needed for native display driver
    }

    Display* invalidateScreen() override {
        clearBuffer();
        return this;
    }

    void render() override {
        // Native display doesn't render to hardware
    }

    Display* drawText(const char *text) override {
        if (text) {
            lastText_ = text;
            addToTextHistory(text);
            drawTextToBuffer(text, 0, 0);
        }
        return this;
    }

    Display* setGlyphMode(FontMode mode) override {
        currentFontMode_ = mode;
        return this;
    }

    Display* renderGlyph(const char* unicodeForGlyph, int xStart, int yStart) override {
        // Track the glyph unicode value for CLI display
        if (unicodeForGlyph) {
            std::string glyphText = std::string("[glyph: ") + unicodeForGlyph + "]";
            lastText_ = glyphText;
            addToTextHistory(glyphText);
            drawRect(xStart, yStart, 16, 16);
        }
        return this;
    }
    
    Display* drawButton(const char *text, int xCenter, int yCenter) override {
        if (text) {
            std::string btnText = std::string("[") + text + "]";
            lastText_ = btnText;
            addToTextHistory(btnText);
            int textWidth = btnText.length() * 6;
            drawTextToBuffer(btnText.c_str(), xCenter - textWidth/2, yCenter - 4);
        }
        return this;
    }

    Display* drawText(const char *text, int xStart, int yStart) override {
        if (text) {
            lastText_ = text;
            addToTextHistory(text);
            drawTextToBuffer(text, xStart, yStart);
        }
        return this;
    }

    Display* drawImage(Image image) override {
        currentImage_ = image;
        drawImage(image, -1, -1);
        return this;
    }

    Display* drawImage(Image image, int xStart, int yStart) override {
        currentImage_ = image;
        int x = image.defaultStartX;
        int y = image.defaultStartY;
        if (xStart != -1) x = xStart;
        if (yStart != -1) y = yStart;
        decodeXBMToBuffer(image.rawImage, image.width, image.height, x, y);
        return this;
    }

    Display* drawBox(int x, int y, int w, int h) override {
        for (int dy = 0; dy < h; dy++) {
            for (int dx = 0; dx < w; dx++) {
                setPixel(x + dx, y + dy, currentDrawColor_ == 1);
            }
        }
        return this;
    }

    Display* drawFrame(int x, int y, int w, int h) override {
        for (int dx = 0; dx < w; dx++) {
            setPixel(x + dx, y, currentDrawColor_ == 1);
            setPixel(x + dx, y + h - 1, currentDrawColor_ == 1);
        }
        for (int dy = 0; dy < h; dy++) {
            setPixel(x, y + dy, currentDrawColor_ == 1);
            setPixel(x + w - 1, y + dy, currentDrawColor_ == 1);
        }
        return this;
    }

    Display* drawGlyph(int x, int y, uint16_t glyph) override {
        std::string glyphText = "[G:" + std::to_string(glyph) + "]";
        drawTextToBuffer(glyphText.c_str(), x, y);
        return this;
    }

    Display* setDrawColor(int color) override {
        currentDrawColor_ = color;
        return this;
    }

    Display* setFont(const uint8_t *font) override {
        return this;
    }

    // CLI access methods
    const std::string& getLastText() const { return lastText_; }
    FontMode getCurrentFontMode() const { return currentFontMode_; }
    
    std::string getFontModeName() const {
        switch (currentFontMode_) {
            case FontMode::TEXT: return "TEXT";
            case FontMode::NUMBER_GLYPH: return "NUMBER";
            case FontMode::LOADING_GLYPH: return "LOADING";
            case FontMode::TEXT_INVERTED_SMALL: return "INV_SM";
            case FontMode::TEXT_INVERTED_LARGE: return "INV_LG";
            default: return "?";
        }
    }
    
    /**
     * Get the last N text strings drawn (most recent first).
     * Useful for showing display history in CLI.
     */
    const std::deque<std::string>& getTextHistory() const {
        return textHistory_;
    }
    
    // Get pixel at position
    bool getPixel(int x, int y) const {
        if (x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT) return false;
        return screenBuffer_[y][x];
    }
    
    // Render display as ASCII art using half-block characters
    // Scale: 2 horizontal pixels per char, 2 vertical pixels per char (using half blocks)
    // Result: 64 chars wide, 32 lines tall
    std::vector<std::string> renderToAscii(int scaleX = 2, int scaleY = 2) const {
        std::vector<std::string> lines;
        
        // Use half-block characters: ▀ (upper), ▄ (lower), █ (both), space (none)
        for (int y = 0; y < HEIGHT; y += scaleY * 2) {
            std::string line;
            for (int x = 0; x < WIDTH; x += scaleX) {
                // Sample pixels for this character cell
                bool upper = false, lower = false;
                
                // Check upper half (first scaleY rows)
                for (int dy = 0; dy < scaleY && !upper; dy++) {
                    for (int dx = 0; dx < scaleX && !upper; dx++) {
                        if (getPixel(x + dx, y + dy)) upper = true;
                    }
                }
                
                // Check lower half (next scaleY rows)
                for (int dy = scaleY; dy < scaleY * 2 && !lower; dy++) {
                    for (int dx = 0; dx < scaleX && !lower; dx++) {
                        if (getPixel(x + dx, y + dy)) lower = true;
                    }
                }
                
                // Select character based on which halves are filled
                if (upper && lower) {
                    line += "█";
                } else if (upper) {
                    line += "▀";
                } else if (lower) {
                    line += "▄";
                } else {
                    line += " ";
                }
            }
            lines.push_back(line);
        }
        
        return lines;
    }

    /*
     * Render display as braille art (U+2800-U+28FF).
     * Each braille character encodes a 2x4 pixel grid with 8 bits of detail,
     * vs 2 bits for half-block characters. Same 64x16 terminal footprint
     * but dramatically better text legibility.
     *
     * Braille dot layout:
     *   (0,0)=0x01  (1,0)=0x08
     *   (0,1)=0x02  (1,1)=0x10
     *   (0,2)=0x04  (1,2)=0x20
     *   (0,3)=0x40  (1,3)=0x80
     */
    std::vector<std::string> renderToBraille() const {
        std::vector<std::string> lines;

        for (int y = 0; y < HEIGHT; y += 4) {
            std::string line;
            for (int x = 0; x < WIDTH; x += 2) {
                uint8_t dots = 0;
                if (getPixel(x,   y))   dots |= 0x01;
                if (getPixel(x,   y+1)) dots |= 0x02;
                if (getPixel(x,   y+2)) dots |= 0x04;
                if (getPixel(x,   y+3)) dots |= 0x40;
                if (getPixel(x+1, y))   dots |= 0x08;
                if (getPixel(x+1, y+1)) dots |= 0x10;
                if (getPixel(x+1, y+2)) dots |= 0x20;
                if (getPixel(x+1, y+3)) dots |= 0x80;

                // UTF-8 encode U+2800+dots (3-byte sequence)
                uint32_t cp = 0x2800 + dots;
                line += static_cast<char>(0xE0 | (cp >> 12));
                line += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                line += static_cast<char>(0x80 | (cp & 0x3F));
            }
            lines.push_back(line);
        }

        return lines;
    }

private:
    bool screenBuffer_[HEIGHT][WIDTH];
    FontMode currentFontMode_ = FontMode::TEXT;
    Image currentImage_;
    std::string lastText_;
    std::deque<std::string> textHistory_;
    static const size_t MAX_TEXT_HISTORY = 4;
    int currentDrawColor_ = 1;

    void addToTextHistory(const std::string& text) {
        // Don't add duplicates of the most recent entry
        if (!textHistory_.empty() && textHistory_.front() == text) {
            return;
        }
        textHistory_.push_front(text);  // Most recent at front
        while (textHistory_.size() > MAX_TEXT_HISTORY) {
            textHistory_.pop_back();
        }
    }
    
    /*
     * Decode XBM bitmap data into the screen buffer.
     * XBM format: LSB-first, row-major, ceil(width/8) bytes per row.
     * This mirrors what U8g2's drawXBMP() does on the ESP32.
     */
    void decodeXBMToBuffer(const unsigned char* data, int imgWidth, int imgHeight, int offsetX, int offsetY) {
        if (!data) return;
        int bytesPerRow = (imgWidth + 7) / 8;
        for (int row = 0; row < imgHeight; row++) {
            for (int col = 0; col < imgWidth; col++) {
                int byteIndex = row * bytesPerRow + col / 8;
                int bitIndex = col % 8;  // LSB first
                bool pixelOn = (data[byteIndex] >> bitIndex) & 1;
                setPixel(offsetX + col, offsetY + row, pixelOn);
            }
        }
    }

    void clearBuffer() {
        memset(screenBuffer_, 0, sizeof(screenBuffer_));
    }
    
    void setPixel(int x, int y, bool on) {
        if (x >= 0 && x < WIDTH && y >= 0 && y < HEIGHT) {
            screenBuffer_[y][x] = on;
        }
    }
    
    void drawRect(int x, int y, int w, int h) {
        for (int i = x; i < x + w && i < WIDTH; i++) {
            setPixel(i, y, true);
            setPixel(i, y + h - 1, true);
        }
        for (int j = y; j < y + h && j < HEIGHT; j++) {
            setPixel(x, j, true);
            setPixel(x + w - 1, j, true);
        }
    }
    
    void drawTextToBuffer(const char* text, int x, int y) {
        if (!text) return;
        
        int cursorX = x;
        while (*text && cursorX < WIDTH) {
            char c = *text++;
            
            // Handle newlines
            if (c == '\n') {
                cursorX = x;
                y += 8;
                continue;
            }
            
            // Only render printable ASCII
            if (c >= ' ' && c <= '~') {
                int fontIndex = c - ' ';
                const uint8_t* glyph = FONT_5X7[fontIndex];
                
                // Draw the glyph with black background so text is
                // always visible over images (CLI has no XOR blending)
                for (int col = 0; col < 6; col++) {
                    uint8_t columnData = (col < 5) ? glyph[col] : 0;
                    for (int row = 0; row < 8; row++) {
                        bool on = (row < 7) && (columnData & (1 << row));
                        setPixel(cursorX + col, y + row, on);
                    }
                }
            }
            
            cursorX += 6; // 5 pixels + 1 pixel spacing
        }
    }
};