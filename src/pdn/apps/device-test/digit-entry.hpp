#pragma once

#include "device/pdn.hpp"

class DigitEntry {
public:
    static constexpr int DIGIT_COUNT = 4;

    /** Clears digit buffers and completion flag. */
    void reset();
    /** Registers button callbacks and draws the title. */
    void mount(PDN* pdn, const char* title);
    /** Removes button callbacks. */
    void dismount(PDN* pdn);
    /** Handles digit entry input each tick. */
    void loop(PDN* pdn);
    /** @return true when all digits have been entered. */
    bool isComplete() const;

    /** Writes the entered digits as a null-terminated string. */
    void getPinString(char* out, size_t outSize) const;
    /** @return the entered value as a base-10 integer. */
    int getNumericValue() const;

private:
    void render(PDN* pdn);

    const char* titleText = "";
    int currentDigit = 0;
    int currentDigitIndex = 0;
    int inputId[DIGIT_COUNT] = {0, 0, 0, 0};
    bool shouldRender = false;
    bool complete = false;
};
