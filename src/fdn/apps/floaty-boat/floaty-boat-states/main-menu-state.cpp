#include "apps/floaty-boat/floaty-boat-states.hpp"
#include "device/remote-device-coordinator.hpp"
#include "device/drivers/logger.hpp"

namespace {
static const char* TAG = "MainMenuState";
static const char* kGameTitle = "FLOATY BOAT";
}

MainMenuState::MainMenuState(ControllerWirelessManager* controllerWirelessManager)
    : TypedState<FDN>(FloatyBoatStateId::MAIN_MENU)
    , controllerWirelessManager(controllerWirelessManager) {}

MainMenuState::~MainMenuState() {}

void MainMenuState::sendGameSelectToConnectedPeers(FDN* fdn) {
    RemoteDeviceCoordinator* remoteDeviceCoordinator = fdn->getRemoteDeviceCoordinator();
    for (SerialIdentifier port : {SerialIdentifier::INPUT_JACK, SerialIdentifier::INPUT_JACK_SECONDARY}) {
        const uint8_t* peerMac = remoteDeviceCoordinator->getPeerMac(port);
        if (peerMac != nullptr) {
            controllerWirelessManager->setMacPeer(peerMac);
            controllerWirelessManager->sendGameSelectPacket(GameSelectId::CONTROLLER_1);
        }
    }
}

void MainMenuState::onStateMounted(FDN* fdn) {
    LOG_W(TAG, "Mounted");
    renderMainMenuScreen(fdn, kGameTitle);

    sendGameSelectToConnectedPeers(fdn);
    gameSelectResendTimer.setTimer(kGameSelectResendIntervalMs);

    controllerWirelessManager->setControllerCommandReceivedCallback(
        std::bind(&MainMenuState::onControllerCommandReceived, this, std::placeholders::_1),
        SerialIdentifier::INPUT_JACK);

    controllerWirelessManager->setControllerCommandReceivedCallback(
        std::bind(&MainMenuState::onControllerCommandReceived, this, std::placeholders::_1),
        SerialIdentifier::INPUT_JACK_SECONDARY);

    parameterizedCallbackFunction onPrimaryButtonPressed = [](void* ctx) {
        static_cast<MainMenuState*>(ctx)->transitionToTutorialState = true;
    };

    fdn->getPrimaryButton()->setButtonPress(
        onPrimaryButtonPressed,
        this,
        ButtonInteraction::PRESS);

    parameterizedCallbackFunction onSecondaryButtonPressed = [](void* ctx) {
        static_cast<MainMenuState*>(ctx)->transitionToGameState = true;
    };

    fdn->getSecondaryButton()->setButtonPress(
        onSecondaryButtonPressed,
        this,
        ButtonInteraction::PRESS);
}

void MainMenuState::onStateLoop(FDN* fdn) {
    if (gameSelectResendTimer.expired()) {
        sendGameSelectToConnectedPeers(fdn);
        gameSelectResendTimer.setTimer(kGameSelectResendIntervalMs);
    }
    renderMainMenuScreen(fdn, kGameTitle);
}

void MainMenuState::onStateDismounted(FDN* fdn) {
    LOG_W(TAG, "Dismounted");
    transitionToTutorialState = false;
    transitionToGameState = false;
    gameSelectResendTimer.invalidate();
    fdn->getPrimaryButton()->removeButtonCallbacks();
    fdn->getSecondaryButton()->removeButtonCallbacks();
    controllerWirelessManager->clearCallback();
}

bool MainMenuState::transitionToTutorial() {
    return transitionToTutorialState;
}

bool MainMenuState::transitionToGame() {
    return transitionToGameState;
}

void MainMenuState::onControllerCommandReceived(ControllerCommand command) {
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
