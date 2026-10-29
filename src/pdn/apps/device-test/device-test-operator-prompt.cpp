#include "apps/device-test/device-test-operator-prompt.hpp"

#include "device/drivers/display.hpp"

namespace {

void drawOption(Display* display, const char* text, int y, bool selected) {
    if (text == nullptr) {
        return;
    }
    if (selected) {
        display->drawButton(text, display->getWidth() / 2, y);
    } else {
        display->drawCenteredText(text, y);
    }
}

}  // namespace

void DeviceTestOperatorPrompt::setTitle(const char* title) {
    titleText = title;
    if (mountedPdn != nullptr) {
        render(mountedPdn);
    }
}

void DeviceTestOperatorPrompt::mount(PDN* pdn,
                                     const char* title,
                                     const char* firstOptionLabel,
                                     const char* secondOptionLabel,
                                     void* ctx,
                                     OperatorPromptCallback onFirstOption,
                                     OperatorPromptCallback onSecondOption) {
    titleText = title;
    firstOptionText = firstOptionLabel;
    secondOptionText = secondOptionLabel;
    callbackCtx = ctx;
    firstOptionCallback = onFirstOption;
    secondOptionCallback = onSecondOption;
    mountedPdn = pdn;
    firstOptionSelected = true;

    render(pdn);

    pdn->getPrimaryButton()->setButtonPress(
        [](void* ctxIn) {
            DeviceTestOperatorPrompt* prompt = static_cast<DeviceTestOperatorPrompt*>(ctxIn);
            prompt->firstOptionSelected = !prompt->firstOptionSelected;
            prompt->render(prompt->mountedPdn);
        },
        this,
        ButtonInteraction::CLICK);

    pdn->getSecondaryButton()->setButtonPress(
        [](void* ctxIn) {
            DeviceTestOperatorPrompt* prompt = static_cast<DeviceTestOperatorPrompt*>(ctxIn);
            if (prompt->firstOptionSelected) {
                if (prompt->firstOptionCallback != nullptr) {
                    prompt->firstOptionCallback(prompt->callbackCtx);
                }
            } else if (prompt->secondOptionCallback != nullptr) {
                prompt->secondOptionCallback(prompt->callbackCtx);
            }
        },
        this,
        ButtonInteraction::CLICK);
}

void DeviceTestOperatorPrompt::dismount(PDN* pdn) {
    pdn->getPrimaryButton()->removeButtonCallbacks();
    pdn->getSecondaryButton()->removeButtonCallbacks();
    firstOptionCallback = nullptr;
    secondOptionCallback = nullptr;
    callbackCtx = nullptr;
    mountedPdn = nullptr;
    firstOptionSelected = true;
}

void DeviceTestOperatorPrompt::render(PDN* pdn) {
    Display* display = pdn->getDisplay();
    display->invalidateScreen()->setGlyphMode(FontMode::TEXT)->drawCenteredText(titleText, 16);
    drawOption(display, firstOptionText, kFirstOptionY, firstOptionSelected);
    drawOption(display, secondOptionText, kSecondOptionY, !firstOptionSelected);
    display->render();
}
