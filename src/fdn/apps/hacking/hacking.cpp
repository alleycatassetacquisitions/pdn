#include "apps/hacking/hacking.hpp"
#include "apps/hacking/states/hacking-show-hint-state.hpp"
#include "apps/fdn-app-ids.hpp"

Hacking::Hacking(FDNConnectWirelessManager* fdnConnectWirelessManager,
                 HackedPlayersManager* hackedPlayersManager,
                 RemoteDeviceCoordinator* remoteDeviceCoordinator)
    : StateMachine(HACKING_APP_ID)
    , fdnConnectWirelessManager(fdnConnectWirelessManager)
    , hackedPlayersManager(hackedPlayersManager)
    , remoteDeviceCoordinator(remoteDeviceCoordinator) {}

Hacking::~Hacking() {}

void Hacking::populateStateMap() {
    auto* showHintState = new HackingShowHintState(
        fdnConnectWirelessManager, hackedPlayersManager, remoteDeviceCoordinator);

    showHintState->addAppTransition(
        std::bind(&HackingShowHintState::shouldTransitionToIdle, showHintState),
        StateId(SYMBOL_MATCH_APP_ID));

    stateMap.push_back(showHintState);
}
