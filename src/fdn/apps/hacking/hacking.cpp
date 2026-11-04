#include "apps/hacking/hacking.hpp"
#include "apps/hacking/states/hacking-show-hint-state.hpp"
#include "apps/fdn-app-ids.hpp"

HackingApp::HackingApp(FDNConnectWirelessManager* fdnConnectWirelessManager,
                       HackedPlayersManager* hackedPlayersManager,
                       RemoteDeviceCoordinator* remoteDeviceCoordinator)
    : StateMachine(HACKING_APP_ID)
    , fdnConnectWirelessManager(fdnConnectWirelessManager)
    , hackedPlayersManager(hackedPlayersManager)
    , remoteDeviceCoordinator(remoteDeviceCoordinator) {}

HackingApp::~HackingApp() {}

void HackingApp::populateStateMap() {
    auto* showHintState = new HackingShowHintState(
        fdnConnectWirelessManager, hackedPlayersManager, remoteDeviceCoordinator);

    showHintState->addAppTransition(
        std::bind(&HackingShowHintState::shouldTransitionToIdle, showHintState),
        StateId(SYMBOL_MATCH_APP_ID));

    stateMap.push_back(showHintState);
}
