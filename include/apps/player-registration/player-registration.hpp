#pragma once

#include "state/state-machine.hpp"
#include "apps/player-registration/player-registration-states.hpp"
#include "game/player.hpp"
#include "device/device.hpp"
#include "device/wireless-manager.hpp"
#include "wireless/remote-debug-manager.hpp"
#include "game/match-manager.hpp"

constexpr int PLAYER_REGISTRATION_APP_ID = 0;

class PlayerRegistrationApp : public StateMachine {
public:
    PlayerRegistrationApp(Player* player, WirelessManager* wirelessManager, MatchManager* matchManager, RemoteDebugManager* remoteDebugManager);
    ~PlayerRegistrationApp();

    void populateStateMap() override;

    // Exit conditions checked by parent state machine
    bool readyForGameplay();

private:
    Player* player;
    WirelessManager* wirelessManager;
    RemoteDebugManager* remoteDebugManager;
    MatchManager* matchManager;
};
