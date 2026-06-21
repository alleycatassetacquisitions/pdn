#include "apps/demo-modules/demo-module-states.hpp"
#include "device/remote-device-coordinator.hpp"
#include "device/drivers/logger.hpp"

namespace {
static const char* TAG = "MainMenuState";
static const char* kStateLabel = "MAIN MENU";
}

MainMenuState::MainMenuState(ControllerWirelessManager* controllerWirelessManager)
    : TypedState<FDN>(DemoModuleStateId::MAIN_MENU)
    , controllerWirelessManager(controllerWirelessManager) {}

MainMenuState::~MainMenuState() {}

void MainMenuState::onStateMounted(FDN* fdn) {
    LOG_W(TAG, "Mounted");
    renderDemoStateLabel(fdn, kStateLabel);

    RemoteDeviceCoordinator* remoteDeviceCoordinator = fdn->getRemoteDeviceCoordinator();
    for (SerialIdentifier port : {SerialIdentifier::INPUT_JACK, SerialIdentifier::INPUT_JACK_SECONDARY}) {
        const uint8_t* peerMac = remoteDeviceCoordinator->getPeerMac(port);
        if (peerMac != nullptr) {
            controllerWirelessManager->setMacPeer(peerMac);
            controllerWirelessManager->sendGameSelectPacket(GameSelectId::CONTROLLER_1);
        }
    }

    controllerWirelessManager->setControllerCommandReceivedCallback(
        std::bind(&MainMenuState::onControllerCommandReceived, this, std::placeholders::_1),
        SerialIdentifier::INPUT_JACK);

    controllerWirelessManager->setControllerCommandReceivedCallback(
        std::bind(&MainMenuState::onControllerCommandReceived, this, std::placeholders::_1),
        SerialIdentifier::INPUT_JACK_SECONDARY);
}

void MainMenuState::onStateLoop(FDN* fdn) {
    renderDemoStateLabel(fdn, kStateLabel);
}

void MainMenuState::onStateDismounted(FDN* fdn) {
    LOG_W(TAG, "Dismounted");
}

bool MainMenuState::transitionToTutorial() {
    return false;
}

bool MainMenuState::transitionToGame() {
    return false;
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