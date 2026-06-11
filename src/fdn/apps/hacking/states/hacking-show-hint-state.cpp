#include "apps/hacking/states/hacking-show-hint-state.hpp"
#include "utils/display-utils.hpp"
#include "device/drivers/logger.hpp"

#define TAG "HACKING_HINT"

HackingShowHintState::HackingShowHintState(
    FDNConnectWirelessManager* fdnConnectWirelessManager,
    HackedPlayersManager* hackedPlayersManager,
    RemoteDeviceCoordinator* remoteDeviceCoordinator)
    : FDNConnectState(remoteDeviceCoordinator, HackingStateId::HACKING_HINT)
    , fdnConnectWirelessManager(fdnConnectWirelessManager)
    , hackedPlayersManager(hackedPlayersManager) {}

HackingShowHintState::~HackingShowHintState() {}

void HackingShowHintState::onStateMounted(FDN* fdn) {
    LOG_I(TAG, "Mounted");
    fdn->getDisplay()
        ->invalidateScreen()
        ->drawText("PLEASE", centeredTextX("PLEASE"), 32)
        ->drawText("DISCONNECT", centeredTextX("DISCONNECT"), 48)
        ->render();

    fdn->getTertiaryButton()->setButtonPress([](void* ctx) {
        static_cast<HackingShowHintState*>(ctx)->transitionToIdle = true;
    }, this, ButtonInteraction::PRESS);
}

void HackingShowHintState::onStateLoop(FDN* fdn) {
    (void)fdn;
}

void HackingShowHintState::onStateDismounted(FDN* fdn) {
    LOG_I(TAG, "Dismounted");
    fdn->getTertiaryButton()->removeButtonCallbacks();
    transitionToIdle = false;
}

bool HackingShowHintState::shouldTransitionToIdle() {
    return transitionToIdle || !isConnected();
}
