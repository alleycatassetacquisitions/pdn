#pragma once

#include "state/state-machine.hpp"
#include "apps/player-registration/player-registration-states.hpp"
#include "apps/pdn-app-ids.hpp"
#include "game/player.hpp"
#include "device/device.hpp"
#include "device/wireless-manager.hpp"
#include "wireless/remote-debug-manager.hpp"
#include "game/match-manager.hpp"

class PlayerRegistrationApp : public StateMachine {
public:
    PlayerRegistrationApp(Player* player, WirelessManager* wirelessManager, MatchManager* matchManager, RemoteDebugManager* remoteDebugManager);
    ~PlayerRegistrationApp();

    void populateStateMap() override;

private:
    Player* player;
    WirelessManager* wirelessManager;
    RemoteDebugManager* remoteDebugManager;
    MatchManager* matchManager;
};
