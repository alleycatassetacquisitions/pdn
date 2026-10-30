#pragma once

#include "device/pdn.hpp"

using OperatorPromptCallback = void (*)(void* ctx);

class DeviceTestOperatorPrompt {
public:
    /** Shows a two-option PASS/FAIL prompt with button navigation. */
    void mount(PDN* pdn, const char* title, const char* firstOptionLabel,
               const char* secondOptionLabel, void* ctx, OperatorPromptCallback onFirstOption,
               OperatorPromptCallback onSecondOption);
    /** Removes button callbacks registered in @ref mount. */
    void dismount(PDN* pdn);
    /** Redraws the prompt when the selection changes. */
    void render(PDN* pdn);

    /** Updates the title line without remounting. */
    void setTitle(const char* title);

private:
    static constexpr int FIRST_OPTION_Y = 36;
    static constexpr int SECOND_OPTION_Y = 56;

    const char* titleText = "";
    const char* firstOptionText = "PASS";
    const char* secondOptionText = "FAIL";
    void* callbackCtx = nullptr;
    OperatorPromptCallback firstOptionCallback = nullptr;
    OperatorPromptCallback secondOptionCallback = nullptr;
    PDN* mountedPdn = nullptr;
    bool firstOptionSelected = true;
};
