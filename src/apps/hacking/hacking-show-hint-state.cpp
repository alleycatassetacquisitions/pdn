#include "apps/hacking/states/hacking-show-hint-state.hpp"
#include "apps/hacking/hacking.hpp"
#include "device/drivers/logger.hpp"
#include "utils/display-utils.hpp"

#define TAG "HACKING_HINT"

HackingShowHintState::HackingShowHintState(
    FDNConnectWirelessManager* fdnConnectWirelessManager,
    HackedPlayersManager* hackedPlayersManager,
    RemoteDeviceCoordinator* remoteDeviceCoordinator)
    : ConnectState(remoteDeviceCoordinator, HackingStateId::HACKING_HINT),
      fdnConnectWirelessManager(fdnConnectWirelessManager),
      hackedPlayersManager(hackedPlayersManager) {}

HackingShowHintState::~HackingShowHintState() {}

void HackingShowHintState::onStateMounted(Device* PDN) {
    LOG_I(TAG, "Mounted - stub");
    PDN->getDisplay()
        ->invalidateScreen()
        ->drawText("PLEASE", centeredTextX("PLEASE"), 32)
        ->drawText("DISCONNECT", centeredTextX("DISCONNECT"), 48)
        ->render();

    PDN->getTertiaryButton()->setButtonPress([](void *ctx) {
        HackingShowHintState* hackingShowHintState = (HackingShowHintState*)ctx;
        hackingShowHintState->transitionToIdle = true;
    }, this, ButtonInteraction::PRESS);
}
void HackingShowHintState::onStateLoop(Device* PDN) {}

void HackingShowHintState::onStateDismounted(Device* PDN) {
    LOG_I(TAG, "Dismounted");
    PDN->getTertiaryButton()->removeButtonCallbacks();
    transitionToIdle = false;
}

bool HackingShowHintState::shouldTransitionToIdle() {
    return transitionToIdle || !isConnected();
}

bool HackingShowHintState::isJackRequired(SerialIdentifier jack) {
    return jack == SerialIdentifier::INPUT_JACK || jack == SerialIdentifier::INPUT_JACK_SECONDARY;
}
