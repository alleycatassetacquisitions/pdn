#include "apps/fdn-connect/fdn-connect.hpp"

FDNConnect::FDNConnect(Player *player, Device *PDN, FDNConnectWirelessManager* fdnConnectWirelessManager) : StateMachine(player, PDN, fdnConnectWirelessManager) {
    this->player = player;
    this->PDN = PDN;
    this->fdnConnectWirelessManager = fdnConnectWirelessManager;
}

void FDNConnect::populateStateMap() {
    FDNConnectInitialState* initialState = new FDNConnectInitialState(PDN->getRemoteDeviceCoordinator());

    stateMap.push_back(initialState);
}
