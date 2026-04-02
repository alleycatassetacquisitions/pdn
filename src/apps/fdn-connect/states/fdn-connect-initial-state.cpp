#include "apps/fdn-connect/fdn-connect-states.hpp"
#include "device/device.hpp"

FDNConnectInitialState::FDNConnectInitialState(RemoteDeviceCoordinator* remoteDeviceCoordinator) : ConnectState(remoteDeviceCoordinator, INITIAL_STATE) {
}

void FDNConnectInitialState::onStateMounted(Device *PDN) {
    
}

void FDNConnectInitialState::onStateLoop(Device *PDN) {
    if(isConnected()) {
        PDN->getDisplay()->invalidateScreen()->drawImage(glassesImage)->render();
    }
}

void FDNConnectInitialState::onStateDismounted(Device *PDN) {
}

bool FDNConnectInitialState::isPrimaryRequired() {
    return true;
}

bool FDNConnectInitialState::isAuxRequired() {
    return false;
}
