#include "apps/fdn-connect/fdn-connect.hpp"

FDNConnect::FDNConnect(Player *player, RemoteDeviceCoordinator* remoteDeviceCoordinator, FDNConnectWirelessManager* fdnConnectWirelessManager) : StateMachine(FDN_CONNECT_APP_ID) {
    this->player = player;
    this->remoteDeviceCoordinator = remoteDeviceCoordinator;
    this->fdnConnectWirelessManager = fdnConnectWirelessManager;
}

FDNConnect::~FDNConnect() {
    player = nullptr;
    remoteDeviceCoordinator = nullptr;
    fdnConnectWirelessManager = nullptr;
}

void FDNConnect::populateStateMap() {
    FDNConnectInitialState* initialState = new FDNConnectInitialState(remoteDeviceCoordinator);

    stateMap.push_back(initialState);
}
