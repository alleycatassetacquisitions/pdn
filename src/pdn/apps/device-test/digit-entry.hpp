#pragma once

#include "device/pdn.hpp"

class DigitEntry {
public:
    static constexpr int kDigitCount = 4;

    void reset();
    void mount(PDN* pdn, const char* title);
    void dismount(PDN* pdn);
    void loop(PDN* pdn);
    bool isComplete() const;

    void getPinString(char* out, size_t outSize) const;
    int getNumericValue() const;

private:
    void render(PDN* pdn);

    const char* titleText = "";
    int currentDigit = 0;
    int currentDigitIndex = 0;
    int inputId[kDigitCount] = {0, 0, 0, 0};
    bool shouldRender = false;
    bool complete = false;
};
