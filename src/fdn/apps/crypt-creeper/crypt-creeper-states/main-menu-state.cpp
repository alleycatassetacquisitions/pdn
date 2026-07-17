#include "apps/crypt-creeper/crypt-creeper-states.hpp"
#include "device/remote-device-coordinator.hpp"
#include "device/drivers/logger.hpp"

namespace {
static const char* TAG = "CryptCreeperMainMenuState";
static const char* kGameTitleLine1 = "CRYPT";
static const char* kGameTitleLine2 = "CREEPER";
}

CryptCreeperMainMenuState::CryptCreeperMainMenuState(ControllerWirelessManager* controllerWirelessManager)
    : TypedState<FDN>(CryptCreeperStateId::MAIN_MENU)
    , controllerWirelessManager(controllerWirelessManager) {}

CryptCreeperMainMenuState::~CryptCreeperMainMenuState() {}

void CryptCreeperMainMenuState::onStateMounted(FDN* fdn) {
    LOG_W(TAG, "Mounted");
    renderMainMenuScreen(fdn, kGameTitleLine1, kGameTitleLine2);

    RemoteDeviceCoordinator* remoteDeviceCoordinator = fdn->getRemoteDeviceCoordinator();
    for (SerialIdentifier port : {SerialIdentifier::INPUT_JACK, SerialIdentifier::INPUT_JACK_SECONDARY}) {
        const uint8_t* peerMac = remoteDeviceCoordinator->getPeerMac(port);
        if (peerMac != nullptr) {
            controllerWirelessManager->setMacPeer(peerMac);
            controllerWirelessManager->sendGameSelectPacket(GameSelectId::CONTROLLER_1);
        }
    }

    controllerWirelessManager->setControllerCommandReceivedCallback(
        std::bind(&CryptCreeperMainMenuState::onControllerCommandReceived, this, std::placeholders::_1),
        SerialIdentifier::INPUT_JACK);

    controllerWirelessManager->setControllerCommandReceivedCallback(
        std::bind(&CryptCreeperMainMenuState::onControllerCommandReceived, this, std::placeholders::_1),
        SerialIdentifier::INPUT_JACK_SECONDARY);

    parameterizedCallbackFunction onPrimaryButtonPressed = [](void* ctx) {
        static_cast<CryptCreeperMainMenuState*>(ctx)->transitionToTutorialState = true;
    };

    fdn->getPrimaryButton()->setButtonPress(
        onPrimaryButtonPressed,
        this,
        ButtonInteraction::PRESS);

    parameterizedCallbackFunction onSecondaryButtonPressed = [](void* ctx) {
        static_cast<CryptCreeperMainMenuState*>(ctx)->transitionToGameState = true;
    };

    fdn->getSecondaryButton()->setButtonPress(
        onSecondaryButtonPressed,
        this,
        ButtonInteraction::PRESS);
}

void CryptCreeperMainMenuState::onStateLoop(FDN* fdn) {
    renderMainMenuScreen(fdn, kGameTitleLine1, kGameTitleLine2);
}

void CryptCreeperMainMenuState::onStateDismounted(FDN* fdn) {
    LOG_W(TAG, "Dismounted");
    transitionToTutorialState = false;
    transitionToGameState = false;
}

bool CryptCreeperMainMenuState::transitionToTutorial() {
    return transitionToTutorialState;
}

bool CryptCreeperMainMenuState::transitionToGame() {
    return transitionToGameState;
}

void CryptCreeperMainMenuState::onControllerCommandReceived(ControllerCommand command) {
    if (command.command != ControllerCmd::INTERACTION_REQUEST || !command.wifiMacAddrValid) {
        return;
    }

    controllerWirelessManager->setMacPeer(command.wifiMacAddr);

    if (command.buttonId == ButtonIdentifier::PRIMARY_BUTTON) {
        controllerWirelessManager->sendGameResponsePacket(GameResponseId::TOP_BUTTON_PRESSED);
    } else if (command.buttonId == ButtonIdentifier::SECONDARY_BUTTON) {
        controllerWirelessManager->sendGameResponsePacket(GameResponseId::BOTTOM_BUTTON_PRESSED);
    }
}