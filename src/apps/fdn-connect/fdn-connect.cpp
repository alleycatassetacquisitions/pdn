#include "apps/fdn-connect/fdn-connect.hpp"

FDNConnect::FDNConnect(Player* player, RemoteDeviceCoordinator* remoteDeviceCoordinator, FDNConnectWirelessManager* fdnConnectWirelessManager)
    : StateMachine(FDN_CONNECT_APP_ID) {
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
    auto* sendConnect    = new FdnSendConnectState(player, remoteDeviceCoordinator, fdnConnectWirelessManager);
    auto* awaitSequence  = new FdnAwaitSequenceState(fdnConnectWirelessManager);
    auto* hackingInput   = new FdnHackingInputState(fdnConnectWirelessManager, remoteDeviceCoordinator, awaitSequence->getSequence());

    sendConnect->addTransition(new StateTransition(
        std::bind(&FdnSendConnectState::transitionToAwaitSequence, sendConnect),
        awaitSequence));

    awaitSequence->addTransition(new StateTransition(
        std::bind(&FdnAwaitSequenceState::transitionToHackingInput, awaitSequence),
        hackingInput));

    stateMap.push_back(sendConnect);
    stateMap.push_back(awaitSequence);
    stateMap.push_back(hackingInput);
}

bool FDNConnect::returnToIdle() {
    int id = currentState->getStateId();
    if (id == FDN_AWAIT_SEQUENCE) {
        return static_cast<FdnAwaitSequenceState*>(currentState)->transitionToIdle();
    }
    if (id == FDN_HACKING_INPUT) {
        return static_cast<FdnHackingInputState*>(currentState)->transitionToIdle();
    }
    return false;
}
