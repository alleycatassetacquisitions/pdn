#pragma once

#include "state/state.hpp"
#include "device/device.hpp"
#include "device/drivers/logger.hpp"
#include "utils/simple-timer.hpp"
#include <stdint.h>
#include <functional>
#include <string>

static const char* INSTRUCTIONS_TAG = "InstructionsState";

struct InstructionsPage {
    std::string lines[3];
    uint8_t lineCount;
};

struct InstructionsConfig {
    const InstructionsPage* pages;
    uint8_t pageCount;
    std::string optionOneLabel;
    std::string optionTwoLabel;
};

class InstructionsState : public State {
public:
    explicit InstructionsState(int stateId, InstructionsConfig config)
        : State(stateId), config(config) {}

    ~InstructionsState() override = default;

    std::function<void()> onBeforeMount;

    void onStateMounted(Device *PDN) override {
        LOG_I(INSTRUCTIONS_TAG, "State mounted");

        if(onBeforeMount) onBeforeMount();

        currentPage       = 0;
        menuIndex         = 0;
        finishedPaging    = config.pageCount == 0;
        displayIsDirty    = false;
        optionOneSelected = false;
        optionTwoSelected = false;

        PDN->getPrimaryButton()->setButtonPress([](void *ctx) {
            InstructionsState* self = static_cast<InstructionsState*>(ctx);
            if(self->finishedPaging) {
                self->menuIndex = (self->menuIndex + 1) % 2;
            } else {
                self->currentPage++;
                if(self->currentPage >= self->config.pageCount) {
                    self->currentPage    = self->config.pageCount - 1;
                    self->finishedPaging = true;
                }
            }
            self->displayIsDirty = true;
        }, this, ButtonInteraction::CLICK);

        PDN->getSecondaryButton()->setButtonPress([](void *ctx) {
            InstructionsState* self = static_cast<InstructionsState*>(ctx);
            if(!self->finishedPaging) return;
            if(self->menuIndex == 0) self->optionOneSelected = true;
            else                     self->optionTwoSelected  = true;
        }, this, ButtonInteraction::CLICK);

        renderUi(PDN);
        if(!finishedPaging) uiPageTimer.setTimer(UI_PAGE_TIMEOUT);
    }

    void onStateLoop(Device *PDN) override {
        if(!finishedPaging && uiPageTimer.expired()) {
            currentPage++;
            if(currentPage >= config.pageCount) {
                currentPage     = config.pageCount - 1;
                finishedPaging  = true;
            } else {
                uiPageTimer.setTimer(UI_PAGE_TIMEOUT);
            }
            renderUi(PDN);
        }
        if(displayIsDirty) {
            renderUi(PDN);
            displayIsDirty = false;
        }
    }

    void onStateDismounted(Device *PDN) override {
        LOG_I(INSTRUCTIONS_TAG, "State dismounted");
        PDN->getPrimaryButton()->removeButtonCallbacks();
        PDN->getSecondaryButton()->removeButtonCallbacks();
        currentPage     = 0;
        menuIndex       = 0;
        finishedPaging  = false;
        displayIsDirty  = false;
        optionOneSelected = false;
        optionTwoSelected  = false;
        uiPageTimer.invalidate();
    }

    bool optionOneSelectedEvent() const { return optionOneSelected; }
    bool optionTwoSelectedEvent() const { return optionTwoSelected; }

private:
    void renderUi(Device *PDN) {
        PDN->getDisplay()->invalidateScreen();
        PDN->getDisplay()->setGlyphMode(FontMode::TEXT);

        if(!finishedPaging) {
            const InstructionsPage& page = config.pages[currentPage];

            static const int yPositions[3][3] = {
                { 36,  0,  0 },  // 1 line
                { 26, 46,  0 },  // 2 lines
                { 18, 34, 50 },  // 3 lines
            };
            const int* yPos = yPositions[page.lineCount - 1];
            for(int i = 0; i < page.lineCount; i++) {
                PDN->getDisplay()->drawText(page.lines[i].c_str(), 0, yPos[i], true);
            }
        } else {
            if(menuIndex == 0) {
                PDN->getDisplay()->drawButton(config.optionOneLabel.c_str(), DISPLAY_WIDTH / 2, 32)
                    ->drawText(config.optionTwoLabel.c_str(), (DISPLAY_WIDTH - 6 * INSTRUCTIONS_CHAR_WIDTH) / 2, 52);
            } else {
                PDN->getDisplay()->drawText(config.optionOneLabel.c_str(), (DISPLAY_WIDTH - 7 * INSTRUCTIONS_CHAR_WIDTH) / 2, 32)
                    ->drawButton(config.optionTwoLabel.c_str(), DISPLAY_WIDTH / 2, 52);
            }
        }

        PDN->getDisplay()->render();
    }

    static constexpr int UI_PAGE_TIMEOUT = 3000;
    static constexpr int DISPLAY_WIDTH   = 128;
    static constexpr int INSTRUCTIONS_CHAR_WIDTH = 10;

    InstructionsConfig config;
    int currentPage     = 0;
    int menuIndex       = 0;
    bool finishedPaging  = false;
    bool displayIsDirty  = false;
    bool optionOneSelected = false;
    bool optionTwoSelected  = false;
    SimpleTimer uiPageTimer;
};
