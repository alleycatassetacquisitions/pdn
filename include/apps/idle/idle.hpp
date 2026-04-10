#pragma once

#include "state/state-machine.hpp"
#include "wireless/remote-player-manager.hpp"
#include "wireless/fdn-connect-wireless-manager.hpp"
#include "device/remote-device-coordinator.hpp"
#include "apps/hacking/hacked-players-manager.hpp"
#include "apps/app-ids.hpp"

enum IdleStateId {
    IDLE = 0,
    PLAYER_DETECTED = 1,
    AUTHORIZED_PDN = 2,
    UNAUTHORIZED_PDN = 3,
    CONNECTION_DETECTED = 4,
    UPLOAD_PENDING = 5,
};

class Idle : public StateMachine {

public:
    explicit Idle(RemotePlayerManager* remotePlayerManager,
                  HackedPlayersManager* hackedPlayersManager,
                  FDNConnectWirelessManager* fdnConnectWirelessManager,
                  RemoteDeviceCoordinator* remoteDeviceCoordinator);
    ~Idle();

    void populateStateMap() override;

private:
    RemotePlayerManager* remotePlayerManager;
    HackedPlayersManager* hackedPlayersManager;
    FDNConnectWirelessManager* fdnConnectWirelessManager;
    RemoteDeviceCoordinator* remoteDeviceCoordinator;
};