#include "apps/player-registration/player-registration-states.hpp"
#include "device/device.hpp"

ChooseRoleState::ChooseRoleState(Player* player) : TypedState<PDN>(PlayerRegistrationStateId::CHOOSE_ROLE) {
    this->player = player;
}

ChooseRoleState::~ChooseRoleState() {
}

void ChooseRoleState::onStateMounted(PDN* pdn) {
    renderUi(pdn);

    pdn->getPrimaryButton()->setButtonPress([](void *ctx) {
        ChooseRoleState* chooseRoleState = (ChooseRoleState*)ctx;
        chooseRoleState->hunterSelected = !chooseRoleState->hunterSelected;
        chooseRoleState->displayIsDirty = true;
    }, this, ButtonInteraction::CLICK);

    pdn->getSecondaryButton()->setButtonPress([](void *ctx) {
        ChooseRoleState* chooseRoleState = (ChooseRoleState*)ctx;
        chooseRoleState->player->setIsHunter(chooseRoleState->hunterSelected);
        chooseRoleState->transitionToWelcomeMessageState = true;
    }, this, ButtonInteraction::CLICK);
}

void ChooseRoleState::onStateLoop(PDN* pdn) {
    if(displayIsDirty) {
        renderUi(pdn);
        displayIsDirty = false;
    }
}

void ChooseRoleState::onStateDismounted(PDN* pdn) {
    pdn->getPrimaryButton()->removeButtonCallbacks();
    pdn->getSecondaryButton()->removeButtonCallbacks();
    transitionToWelcomeMessageState = false;
    displayIsDirty = false;
    hunterSelected = true;
}

bool ChooseRoleState::transitionToWelcomeMessage() {
    return transitionToWelcomeMessageState;
}

void ChooseRoleState::renderUi(PDN* pdn) {
    pdn->getDisplay()->invalidateScreen();

    pdn->getDisplay()->setGlyphMode(FontMode::TEXT)
        ->drawText("Choose Role", 3, 16);
    
    if(hunterSelected) {
        pdn->getDisplay()->drawButton("HUNTER", 64, 36)
            ->drawText("BOUNTY", 25, 56);
    } else {
        pdn->getDisplay()->drawText("HUNTER", 25, 36)
            ->drawButton("BOUNTY", 64, 56);
    }
    
    pdn->getDisplay()->render();
}