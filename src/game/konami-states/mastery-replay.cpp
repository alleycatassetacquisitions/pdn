#include "game/konami-states/mastery-replay.hpp"
#include "device/device.hpp"

MasteryReplay::MasteryReplay(int stateId, GameType gameType) : State(stateId) {
    this->gameType = gameType;
}

MasteryReplay::~MasteryReplay() {
}

void MasteryReplay::onStateMounted(Device* PDN) {
    renderUi(PDN);

    // UP button: toggle between Easy and Hard mode
    PDN->getPrimaryButton()->setButtonPress([](void* ctx) {
        MasteryReplay* state = (MasteryReplay*)ctx;
        state->easyModeSelected = !state->easyModeSelected;
        state->displayIsDirty = true;
    }, this, ButtonInteraction::CLICK);

    // DOWN button: select highlighted mode
    PDN->getSecondaryButton()->setButtonPress([](void* ctx) {
        MasteryReplay* state = (MasteryReplay*)ctx;
        if (state->easyModeSelected) {
            state->transitionToEasyModeState = true;
        } else {
            state->transitionToHardModeState = true;
        }
    }, this, ButtonInteraction::CLICK);
}

void MasteryReplay::onStateLoop(Device* PDN) {
    if (displayIsDirty) {
        renderUi(PDN);
        displayIsDirty = false;
    }
}

void MasteryReplay::onStateDismounted(Device* PDN) {
    PDN->getPrimaryButton()->removeButtonCallbacks();
    PDN->getSecondaryButton()->removeButtonCallbacks();
    transitionToEasyModeState = false;
    transitionToHardModeState = false;
    displayIsDirty = false;
    easyModeSelected = true;
}

bool MasteryReplay::transitionToEasyMode() {
    return transitionToEasyModeState;
}

bool MasteryReplay::transitionToHardMode() {
    return transitionToHardModeState;
}

void MasteryReplay::renderUi(Device* PDN) {
    PDN->getDisplay()->invalidateScreen();

    const char* gameName = getGameDisplayName(gameType);

    // Line 1: Game name (centered at x=64)
    PDN->getDisplay()->setGlyphMode(FontMode::TEXT)
        ->drawText(gameName, 5, 8);

    // Line 2: "-- MASTERED --" (centered)
    PDN->getDisplay()->drawText("-- MASTERED --", 10, 20);

    // Line 3 & 4: Easy Mode / Hard Mode with highlighting
    if (easyModeSelected) {
        PDN->getDisplay()->drawButton("Easy Mode", 64, 32)
            ->drawText("Hard Mode", 25, 44);
    } else {
        PDN->getDisplay()->drawText("Easy Mode", 25, 32)
            ->drawButton("Hard Mode", 64, 44);
    }

    // Line 5: Instructions
    PDN->getDisplay()->drawText("UP:cycle DOWN:ok", 5, 58);

    PDN->getDisplay()->render();
}
