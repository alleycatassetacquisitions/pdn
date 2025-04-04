#include "game/quickdraw-states.hpp"

ChooseRoleState::ChooseRoleState(Player* player) : State(QuickdrawStateId::CHOOSE_ROLE) {
    this->player = player;
}

ChooseRoleState::~ChooseRoleState() {
}

void ChooseRoleState::onStateMounted(Device *PDN) {
    renderUi(PDN);

    PDN->setButtonClick(ButtonInteraction::CLICK, ButtonIdentifier::PRIMARY_BUTTON, [](void *ctx) {
        ChooseRoleState* chooseRoleState = (ChooseRoleState*)ctx;
        chooseRoleState->hunterSelected = !chooseRoleState->hunterSelected;
        chooseRoleState->displayIsDirty = true;
    }, this);

    PDN->setButtonClick(ButtonInteraction::CLICK, ButtonIdentifier::SECONDARY_BUTTON, [](void *ctx) {
        ChooseRoleState* chooseRoleState = (ChooseRoleState*)ctx;
        chooseRoleState->player->setIsHunter(chooseRoleState->hunterSelected);
        chooseRoleState->transitionToAllegiancePickerState = true;
    }, this);
}

void ChooseRoleState::onStateLoop(Device *PDN) {
    if(displayIsDirty) {
        renderUi(PDN);
        displayIsDirty = false;
    }
}

void ChooseRoleState::onStateDismounted(Device *PDN) {
    PDN->removeButtonCallbacks(ButtonIdentifier::PRIMARY_BUTTON);
    PDN->removeButtonCallbacks(ButtonIdentifier::SECONDARY_BUTTON);
    transitionToAllegiancePickerState = false;
    displayIsDirty = false;
    hunterSelected = true;
}

bool ChooseRoleState::transitionToAllegiancePicker() {
    return transitionToAllegiancePickerState;
}

void ChooseRoleState::renderUi(Device *PDN) {
    PDN->invalidateScreen();

    PDN->setGlyphMode(FontMode::TEXT)
        ->drawText("Choose Role", 3, 16);
    
    if(hunterSelected) {
        PDN->drawButton("HUNTER", 64, 36)
            ->drawText("BOUNTY", 25, 56);
    } else {
        PDN->drawText("HUNTER", 25, 36)
            ->drawButton("BOUNTY", 64, 56);
    }
    
    PDN->render();
}