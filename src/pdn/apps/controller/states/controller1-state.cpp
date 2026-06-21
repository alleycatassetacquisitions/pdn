#include "apps/controller/controller-states.hpp"
#include "device/device.hpp"
#include "device/drivers/button.hpp"
#include "device/drivers/logger.hpp"

namespace {
static const char* TAG = "Controller1State";
static const char* kStateLabel = "CONTROLLER 1";
}

Controller1State::Controller1State(ControllerWirelessManager* controllerWirelessManager) : TypedState<PDN>(ControllerStateId::CONTROLLER1) {
    this->controllerWirelessManager = controllerWirelessManager;
}

Controller1State::~Controller1State() {
    this->controllerWirelessManager = nullptr;
}

void Controller1State::onStateMounted(PDN* pdn) {
    LOG_W(TAG, "Mounted");
    auto* display = pdn->getDisplay();
    display->invalidateScreen()->setGlyphMode(FontMode::TEXT);
    display->drawCenteredText(kStateLabel, 32);
    display->render();

    pdn->getPrimaryButton()->setButtonPress(
        [](void *ctx) {
            static_cast<Controller1State*>(ctx)
                ->controllerWirelessManager
                ->sendControllerCommandPacket(
                    ControllerCmd::INTERACTION_REQUEST,
                    ButtonIdentifier::PRIMARY_BUTTON,
                    ButtonInteraction::CLICK,
                    SerialIdentifier::OUTPUT_JACK
                );
        }, this, ButtonInteraction::CLICK);

    pdn->getSecondaryButton()->setButtonPress(
        [](void *ctx) {
            static_cast<Controller1State*>(ctx)
                ->controllerWirelessManager
                ->sendControllerCommandPacket(
                    ControllerCmd::INTERACTION_REQUEST,
                    ButtonIdentifier::SECONDARY_BUTTON,
                    ButtonInteraction::CLICK,
                    SerialIdentifier::OUTPUT_JACK
                );
        }, this, ButtonInteraction::CLICK);

    controllerWirelessManager->setGameResponseReceivedCallback(std::bind(&Controller1State::onGameResponseCommandReceived, this, std::placeholders::_1));
}

void Controller1State::onStateLoop(PDN* pdn) {
    if (lastPressedButton == ButtonIdentifier::PRIMARY_BUTTON) {
        pdn->getDisplay()->invalidateScreen()->setGlyphMode(FontMode::TEXT)->drawCenteredText("top", 32)->render();
    }
    else if (lastPressedButton == ButtonIdentifier::SECONDARY_BUTTON) {
        pdn->getDisplay()->invalidateScreen()->setGlyphMode(FontMode::TEXT)->drawCenteredText("bottom", 32)->render();
    }
}

void Controller1State::onStateDismounted(PDN* pdn) {
    LOG_W(TAG, "Dismounted");
}

void Controller1State::sendButtonMessage(ButtonIdentifier buttonId, ButtonInteraction interaction) {
    controllerWirelessManager->sendControllerCommandPacket(ControllerCmd::INTERACTION_REQUEST, buttonId, interaction, SerialIdentifier::OUTPUT_JACK);
}

void Controller1State::onGameResponseCommandReceived(GameResponseCommand command) {
    if (command.responseId == GameResponseId::TOP_BUTTON_PRESSED) {
        lastPressedButton = ButtonIdentifier::PRIMARY_BUTTON;
    }
    else if (command.responseId == GameResponseId::BOTTOM_BUTTON_PRESSED) {
        lastPressedButton = ButtonIdentifier::SECONDARY_BUTTON;
    }
}