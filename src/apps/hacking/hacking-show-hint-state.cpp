#include "apps/hacking/states/hacking-show-hint-state.hpp"
#include "apps/hacking/hacking.hpp"
#include "device/drivers/logger.hpp"

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
        ->drawText("HACKING", 28, 32)
        ->render();
}

void HackingShowHintState::onStateLoop(Device* PDN) {}

void HackingShowHintState::onStateDismounted(Device* PDN) {
    LOG_I(TAG, "Dismounted");
}

bool HackingShowHintState::isJackRequired(SerialIdentifier jack) {
    return jack == SerialIdentifier::INPUT_JACK || jack == SerialIdentifier::INPUT_JACK_SECONDARY;
}
