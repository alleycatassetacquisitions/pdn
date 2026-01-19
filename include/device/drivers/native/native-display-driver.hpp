#pragma once

#include "device/drivers/driver-interface.hpp"

class NativeDisplayDriver : public DisplayDriverInterface {
    public:
    NativeDisplayDriver(std::string name) : DisplayDriverInterface(name) {
    }

    ~NativeDisplayDriver() override {
    }

    int initialize() override {
        return 0;
    }

    void exec() override {
    }

    Display* invalidateScreen() override {
        return this;
    }

    void render() override {
    }

    Display* drawText(const char *text) override {
        return this;
    }

    Display* setGlyphMode(FontMode mode) override {
        return this;
    }

    Display* renderGlyph(const char* unicodeForGlyph, int xStart, int yStart) override {
        return this;
    }
    
    Display* drawButton(const char *text, int xCenter, int yCenter) override {
        return this;
    }

    Display* drawText(const char *text, int xStart, int yStart) override {
        return this;
    }

    Display* drawImage(Image image) override {
        return this;
    }

    Display* drawImage(Image image, int xStart, int yStart) override {
        return this;
    }

    private:
    uint8_t screenBuffer[128][64];
    FontMode currentFontMode = FontMode::TEXT;
    Image currentImage;
};