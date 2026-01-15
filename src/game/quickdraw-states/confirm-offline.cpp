#include "game/quickdraw-states.hpp"
#include "game/quickdraw-resources.hpp"
#include "logger.hpp"

static const char* TAG = "ConfirmOfflineState";

ConfirmOfflineState::ConfirmOfflineState(Player* player) : State(QuickdrawStateId::CONFIRM_OFFLINE) {
    this->player = player;
    LOG_I(TAG, "ConfirmOfflineState mounted");
}

ConfirmOfflineState::~ConfirmOfflineState() {
}

void ConfirmOfflineState::onStateMounted(Device *PDN) {
    renderUi(PDN);
    LOG_I(TAG, "ConfirmOfflineState mounted - setting up button callbacks");
    PDN->getPrimaryButton()->setButtonPress([](void *ctx) {
        ConfirmOfflineState* confirmOfflineState = (ConfirmOfflineState*)ctx;
        confirmOfflineState->menuIndex++;
        if(confirmOfflineState->menuIndex > 1) {
            confirmOfflineState->menuIndex = 0;
        }
        confirmOfflineState->displayIsDirty = true;
    }, this, ButtonInteraction::CLICK);
    PDN->getSecondaryButton()->setButtonPress([](void *ctx) {
        ConfirmOfflineState* confirmOfflineState = (ConfirmOfflineState*)ctx;
        int menuIndex = confirmOfflineState->menuIndex;
        if(menuIndex == 0) {
            confirmOfflineState->transitionToChooseRoleState = true; 
        }
        else if(menuIndex == 1) {
            confirmOfflineState->transitionToPlayerRegistrationState = true;
        }
    }, this, ButtonInteraction::CLICK);
    uiPageTimer.setTimer(UI_PAGE_TIMEOUT);
}

void ConfirmOfflineState::onStateLoop(Device *PDN) {
    if(uiPageTimer.expired()) {
        uiPage++;
        if(uiPage < 3) {
            uiPageTimer.setTimer(UI_PAGE_TIMEOUT);
            renderUi(PDN);
        }else if(uiPage == 3) {
            finishedPaging = true;
        }
    }
    if(displayIsDirty) {
        renderUi(PDN);
        displayIsDirty = false;
    }
}

void ConfirmOfflineState::onStateDismounted(Device *PDN) {
    PDN->getPrimaryButton()->removeButtonCallbacks();
    PDN->getSecondaryButton()->removeButtonCallbacks();
    uiPage = 0;
    uiPageTimer.invalidate();
    finishedPaging = false;
    menuIndex = 0;
    transitionToChooseRoleState = false;
    transitionToPlayerRegistrationState = false;
}

bool ConfirmOfflineState::transitionToChooseRole() {
    return transitionToChooseRoleState;
}

bool ConfirmOfflineState::transitionToPlayerRegistration() {
    return transitionToPlayerRegistrationState;
}

void ConfirmOfflineState::renderUi(Device *PDN) {
    PDN->getDisplay()->invalidateScreen();
    
    if(uiPage == 0) {
        // Page 0: "Unable to locate asset" - centered and split into multiple lines if needed
        PDN->getDisplay()->setGlyphMode(FontMode::TEXT)
            ->drawText("Unable to", 12, 26)
            ->drawText("Locate Asset", 0, 42);
    } 
    else if(uiPage == 1) {
        // Page 1: "Proceed with pairing code?" - centered and split
        int digitWidth = 15;
        int digitSpacing = 8;
        int totalWidth = (4 * digitWidth) + (3 * digitSpacing);
        int startX = (128 - totalWidth) / 2;

        PDN->getDisplay()->setGlyphMode(FontMode::TEXT)
            ->drawText("Proceed with", 3, 16)
            ->drawText("Pairing Code", 3, 32)
            ->setGlyphMode(FontMode::NUMBER_GLYPH)
            ->renderGlyph(digitGlyphs[getDigitGlyphForIDIndex(0)], startX, 56)
            ->renderGlyph(digitGlyphs[getDigitGlyphForIDIndex(1)], startX + digitWidth + digitSpacing, 56)
            ->renderGlyph(digitGlyphs[getDigitGlyphForIDIndex(2)], startX + (2 * (digitWidth + digitSpacing)), 56)
            ->renderGlyph(digitGlyphs[getDigitGlyphForIDIndex(3)], startX + (3 * (digitWidth + digitSpacing)), 56);
    } 
    else if(uiPage == 2 || finishedPaging) {
        // Page 2: ID digits with instructions - centered layout
        // Calculate the starting X position to center the 4 digits (each 15px wide with ~8px spacing)
        int digitWidth = 15;
        int digitSpacing = 8;
        int totalWidth = (4 * digitWidth) + (3 * digitSpacing);
        int startX = (128 - totalWidth) / 2;
        
        PDN->getDisplay()->setGlyphMode(FontMode::NUMBER_GLYPH)
            ->renderGlyph(digitGlyphs[getDigitGlyphForIDIndex(0)], startX, 18)
            ->renderGlyph(digitGlyphs[getDigitGlyphForIDIndex(1)], startX + digitWidth + digitSpacing, 18)
            ->renderGlyph(digitGlyphs[getDigitGlyphForIDIndex(2)], startX + (2 * (digitWidth + digitSpacing)), 18)
            ->renderGlyph(digitGlyphs[getDigitGlyphForIDIndex(3)], startX + (3 * (digitWidth + digitSpacing)), 18);

        if(menuIndex == 0) {
            PDN->getDisplay()->setGlyphMode(FontMode::TEXT)
            ->drawButton("Confirm", 64, 36)
            ->drawText("Reset", 40, 60);
        } else if(menuIndex == 1) {
            PDN->getDisplay()->setGlyphMode(FontMode::TEXT)
                ->drawText("Confirm", 25, 36)
                ->drawButton("Reset", 64, 60);
        }
    }
    
    PDN->getDisplay()->render();
}

int ConfirmOfflineState::getDigitGlyphForIDIndex(int index) {
    return player->getUserID().c_str()[index] - '0';
}