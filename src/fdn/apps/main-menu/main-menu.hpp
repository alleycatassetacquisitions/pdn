#pragma once

#include "state/state-machine.hpp"
#include "device/fdn.hpp"
#include "wireless/remote-player-manager.hpp"
#include "device/remote-device-coordinator.hpp"

class MainMenu : public StateMachine {
public:
    MainMenu(FDN* fdn, RemotePlayerManager* remotePlayerManager);
    ~MainMenu();

    void populateStateMap() override;

private:
    RemotePlayerManager* remotePlayerManager;
    RemoteDeviceCoordinator* remoteDeviceCoordinator;
};
