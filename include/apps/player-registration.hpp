#pragma once

#include "state/state-machine.hpp"
#include "game/quickdraw-states.hpp"
#include "game/progress-manager.hpp"
#include "wireless/remote-debug-manager.hpp"

constexpr int PLAYER_REGISTRATION_APP_ID = 2;

class PlayerRegistrationApp : public StateMachine {
public:
    PlayerRegistrationApp(Player* player, WirelessManager* wirelessManager,
                          RemoteDebugManager* remoteDebugManager,
                          ProgressManager* progressManager);
    ~PlayerRegistrationApp();
    void populateStateMap() override;

    // External trigger: registration complete, ready for gameplay
    bool readyForGameplay();

private:
    Player* player;
    WirelessManager* wirelessManager;
    RemoteDebugManager* remoteDebugManager;
    ProgressManager* progressManager;
    bool registrationComplete = false;
};
