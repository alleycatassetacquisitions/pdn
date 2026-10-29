#include "apps/device-test/digit-entry.hpp"

#include "game/quickdraw-resources.hpp"

#include <cstdio>

void DigitEntry::reset() {
    currentDigit = 0;
    currentDigitIndex = 0;
    complete = false;
    shouldRender = true;
    for (int i = 0; i < kDigitCount; i++) {
        inputId[i] = 0;
    }
}

void DigitEntry::mount(PDN* pdn, const char* title) {
    titleText = title;
    reset();
    render(pdn);

    pdn->getPrimaryButton()->setButtonPress(
        [](void* ctx) {
            DigitEntry* entry = static_cast<DigitEntry*>(ctx);
            entry->currentDigit++;
            if (entry->currentDigit > 9) {
                entry->currentDigit = 0;
            }
            entry->shouldRender = true;
        },
        this,
        ButtonInteraction::CLICK);

    pdn->getSecondaryButton()->setButtonPress(
        [](void* ctx) {
            DigitEntry* entry = static_cast<DigitEntry*>(ctx);
            entry->inputId[entry->currentDigitIndex] = entry->currentDigit;
            entry->currentDigitIndex++;
            entry->currentDigit = 0;
            if (entry->currentDigitIndex >= kDigitCount) {
                entry->complete = true;
            } else {
                entry->shouldRender = true;
            }
        },
        this,
        ButtonInteraction::CLICK);
}

void DigitEntry::dismount(PDN* pdn) {
    pdn->getPrimaryButton()->removeButtonCallbacks();
    pdn->getSecondaryButton()->removeButtonCallbacks();
}

void DigitEntry::loop(PDN* pdn) {
    if (shouldRender) {
        render(pdn);
        shouldRender = false;
    }
}

bool DigitEntry::isComplete() const {
    return complete;
}

void DigitEntry::getPinString(char* out, size_t outSize) const {
    if (out == nullptr || outSize < 5) {
        return;
    }
    snprintf(out, outSize, "%d%d%d%d", inputId[0], inputId[1], inputId[2], inputId[3]);
}

int DigitEntry::getNumericValue() const {
    return inputId[0] * 1000 + inputId[1] * 100 + inputId[2] * 10 + inputId[3];
}

void DigitEntry::render(PDN* pdn) {
    Display* display = pdn->getDisplay();
    display->invalidateScreen()->setGlyphMode(FontMode::TEXT)->drawText(titleText, 8, 16);
    display->setGlyphMode(FontMode::NUMBER_GLYPH);

    const int xPositions[kDigitCount] = {20, 44, 68, 92};
    for (int i = 0; i < currentDigitIndex; i++) {
        display->renderGlyph(digitGlyphs[inputId[i]], xPositions[i], 40);
    }
    if (currentDigitIndex < kDigitCount) {
        display->renderGlyph(digitGlyphs[currentDigit], xPositions[currentDigitIndex], 40);
    }
    display->render();
}
