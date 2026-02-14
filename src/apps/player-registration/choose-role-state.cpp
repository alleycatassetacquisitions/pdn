#include "apps/player-registration/player-registration-states.hpp"
#include "device/device.hpp"

ChooseRoleState::ChooseRoleState(Player* player) : State(PlayerRegistrationStateId::CHOOSE_ROLE) {
    this->player = player;
}

ChooseRoleState::~ChooseRoleState() {
}

void ChooseRoleState::onStateMounted(Device *PDN) {
    renderUi(PDN);

    PDN->getPrimaryButton()->setButtonPress([](void *ctx) {
        ChooseRoleState* chooseRoleState = (ChooseRoleState*)ctx;
        chooseRoleState->hunterSelected = !chooseRoleState->hunterSelected;
        chooseRoleState->displayIsDirty = true;
    }, this, ButtonInteraction::CLICK);

    PDN->getSecondaryButton()->setButtonPress([](void *ctx) {
        ChooseRoleState* chooseRoleState = (ChooseRoleState*)ctx;
        chooseRoleState->player->setIsHunter(chooseRoleState->hunterSelected);
        chooseRoleState->transitionToWelcomeMessageState = true;
    }, this, ButtonInteraction::CLICK);
}

void ChooseRoleState::onStateLoop(Device *PDN) {
    if(displayIsDirty) {
        renderUi(PDN);
        displayIsDirty = false;
    }
}

void ChooseRoleState::onStateDismounted(Device *PDN) {
    PDN->getPrimaryButton()->removeButtonCallbacks();
    PDN->getSecondaryButton()->removeButtonCallbacks();
    transitionToWelcomeMessageState = false;
    displayIsDirty = false;
    hunterSelected = true;
}

bool ChooseRoleState::transitionToWelcomeMessage() {
    return transitionToWelcomeMessageState;
}

void ChooseRoleState::renderUi(Device *PDN) {
    PDN->getDisplay()->invalidateScreen();

    PDN->getDisplay()->setGlyphMode(FontMode::TEXT)
        ->drawText("Choose Role", 3, 16);
    
    if(hunterSelected) {
        PDN->getDisplay()->drawButton("HUNTER", 64, 36)
            ->drawText("BOUNTY", 25, 56);
    } else {
        PDN->getDisplay()->drawText("HUNTER", 25, 36)
            ->drawButton("BOUNTY", 64, 56);
    }
    
    PDN->getDisplay()->render();
}