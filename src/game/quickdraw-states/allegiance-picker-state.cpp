#include "game/quickdraw-states.hpp"
#include "game/quickdraw.hpp"

AllegiancePickerState::AllegiancePickerState(Player* player) : State(QuickdrawStateId::ALLEGIANCE_PICKER) {
    this->player = player;
}

AllegiancePickerState::~AllegiancePickerState() {
}

void AllegiancePickerState::onStateMounted(Device *PDN) {
    renderUi(PDN);

    PDN->getPrimaryButton()->setButtonPress([](void *ctx) {
        AllegiancePickerState* allegiancePickerState = (AllegiancePickerState*)ctx;
        allegiancePickerState->currentAllegiance++;
        if(allegiancePickerState->currentAllegiance == 4) {
                allegiancePickerState->currentAllegiance = 0;
            }
            allegiancePickerState->displayIsDirty = true;
        }, this, ButtonInteraction::CLICK);

    PDN->getSecondaryButton()->setButtonPress([](void *ctx) {
        AllegiancePickerState* allegiancePickerState = (AllegiancePickerState*)ctx;
        allegiancePickerState->player->setAllegiance(allegiancePickerState->allegianceArray[allegiancePickerState->currentAllegiance]);
        allegiancePickerState->transitionToWelcomeMessageState = true;
    }, this, ButtonInteraction::CLICK);
}

void AllegiancePickerState::onStateLoop(Device *PDN) {
    if(displayIsDirty) {
        renderUi(PDN);
        displayIsDirty = false;
    }
}

void AllegiancePickerState::onStateDismounted(Device *PDN) {
    PDN->getPrimaryButton()->removeButtonCallbacks();
    PDN->getSecondaryButton()->removeButtonCallbacks();
    transitionToWelcomeMessageState = false;
    currentAllegiance = 0;
    displayIsDirty = false;
}

bool AllegiancePickerState::transitionToWelcomeMessage() {
    return transitionToWelcomeMessageState;
}

void AllegiancePickerState::renderUi(Device *PDN) {
    PDN->getDisplay()->invalidateScreen()
    ->drawImage(Quickdraw::getImageForAllegiance(allegianceArray[currentAllegiance], ImageType::LOGO_LEFT))
    ->drawImage(Quickdraw::getImageForAllegiance(allegianceArray[currentAllegiance], ImageType::STAMP))
    ->render();
}