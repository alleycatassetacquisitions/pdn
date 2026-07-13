#pragma once

#include "image.hpp"
#include <vector>

constexpr int DISPLAY_INSTRUCTION_CAPACITY = 20;
constexpr int DISPLAY_TEXT_CAPACITY = 32;

enum class DisplayType {
    SSD1306,
    SSD1309,
};

struct Cursor {
    int x = 0;
    int y = 0;
};

enum class FontMode {
    TEXT,
    NUMBER_GLYPH,
    LOADING_GLYPH,
    TEXT_INVERTED_SMALL,
    TEXT_INVERTED_LARGE,
    SYMBOL_GLYPH
};

enum class HAnchor {
    LEFT,
    CENTER_LEFT,
    CENTER,
    CENTER_RIGHT,
    RIGHT,
};

enum class VAnchor {
    TOP,
    CENTER,
    BOTTOM
};

enum class DrawType {
    TEXT,
    IMAGE,
    BUTTON
};

struct DrawCoordinates {
    int x = 0;
    int y = 0;
    HAnchor xAnchor = HAnchor::LEFT;
    VAnchor yAnchor = VAnchor::TOP;
};

struct ImagePayload {
    Image image;
    DrawCoordinates coordinates;
};

struct TextPayload {
    const char* text;
    DrawCoordinates coordinates;
    FontMode fontMode;

};

struct ButtonPayload {
    const char* text;
    DrawCoordinates coordinates;
    FontMode fontMode;
    bool isUTF8 = false;
};

struct DrawInstruction {
    DrawType drawType;
    union {
        ImagePayload imagePayload;
        TextPayload textPayload;
        ButtonPayload buttonPayload;
    };

    DrawInstruction(TextPayload textPayload) : drawType(DrawType::TEXT), textPayload(textPayload) {}
    DrawInstruction(ImagePayload imagePayload) : drawType(DrawType::IMAGE), imagePayload(imagePayload) {}
    DrawInstruction(ButtonPayload buttonPayload) : drawType(DrawType::BUTTON), buttonPayload(buttonPayload) {}
};

class DisplayRender {
public:
    DisplayRender() {
        instructions.reserve(DISPLAY_INSTRUCTION_CAPACITY);
    }

    virtual ~DisplayRender() = default;

    int addText(TextPayload text) {
        if (instructions.size() >= DISPLAY_INSTRUCTION_CAPACITY) {
            return -1;
        }
        instructions.emplace_back(DrawInstruction(text));
        return instructions.size() - 1;
    }

    int addImage(ImagePayload image) {
        if (instructions.size() >= DISPLAY_INSTRUCTION_CAPACITY) {
            return -1;
        }
        instructions.emplace_back(DrawInstruction(image));
        return instructions.size() - 1;
    }

    int addButton(ButtonPayload button) {
        if (instructions.size() >= DISPLAY_INSTRUCTION_CAPACITY) {
            return -1;
        }
        instructions.emplace_back(DrawInstruction(button));
        return instructions.size() - 1;
    }

    void clearInstructions() {
        instructions.erase(instructions.begin(), instructions.end());
    }

    std::vector<DrawInstruction> instructions;
};