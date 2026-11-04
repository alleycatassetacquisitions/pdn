#pragma once

#include "state/state-machine.hpp"
#include "device/fdn.hpp"
#include "wireless/remote-player-manager.hpp"
#include "device/remote-device-coordinator.hpp"

class MainMenuApp : public StateMachine {
public:
    /** Constructs the FDN main-menu app state machine. */
    MainMenuApp(FDN* fdn, RemotePlayerManager* remotePlayerManager);
    /** Clears non-owning manager pointers. */
    ~MainMenuApp();

    void populateStateMap() override;

private:
    RemotePlayerManager* remotePlayerManager;
    RemoteDeviceCoordinator* remoteDeviceCoordinator;
};
