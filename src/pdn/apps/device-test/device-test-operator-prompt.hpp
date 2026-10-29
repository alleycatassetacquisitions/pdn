#pragma once

#include "device/pdn.hpp"

using OperatorPromptCallback = void (*)(void* ctx);

class DeviceTestOperatorPrompt {
public:
    void mount(PDN* pdn,
               const char* title,
               const char* firstOptionLabel,
               const char* secondOptionLabel,
               void* ctx,
               OperatorPromptCallback onFirstOption,
               OperatorPromptCallback onSecondOption);
    void dismount(PDN* pdn);
    void render(PDN* pdn);

    void setTitle(const char* title);

private:
    static constexpr int kFirstOptionY = 36;
    static constexpr int kSecondOptionY = 56;

    const char* titleText = "";
    const char* firstOptionText = "PASS";
    const char* secondOptionText = "FAIL";
    void* callbackCtx = nullptr;
    OperatorPromptCallback firstOptionCallback = nullptr;
    OperatorPromptCallback secondOptionCallback = nullptr;
    PDN* mountedPdn = nullptr;
    bool firstOptionSelected = true;
};
