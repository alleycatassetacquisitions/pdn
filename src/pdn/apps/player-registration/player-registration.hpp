#pragma once

#include "state/state-machine.hpp"
#include "apps/player-registration/player-registration-states.hpp"
#include "game/player.hpp"
#include "device/device.hpp"
#include "device/wireless-manager.hpp"
#include "wireless/remote-debug-manager.hpp"
#include "game/match-manager.hpp"

class FirmwareUpdateManager;

constexpr int PLAYER_REGISTRATION_APP_ID = 0;

class PlayerRegistrationApp : public StateMachine {
public:
    /**
     * firmwareUpdateManager may be null (no firmware store): the pairing-code
     * screen then offers no seed run.
     */
    PlayerRegistrationApp(Player* player, WirelessManager* wirelessManager, MatchManager* matchManager,
                          RemoteDebugManager* remoteDebugManager, FirmwareUpdateManager* firmwareUpdateManager);
    ~PlayerRegistrationApp();

    void populateStateMap() override;

    // Exit conditions checked by parent state machine
    bool readyForGameplay();
    /** True while the pairing-code entry screen is showing. */
    bool isOnPairingCodeEntry();

private:
    Player* player;
    WirelessManager* wirelessManager;
    RemoteDebugManager* remoteDebugManager;
    MatchManager* matchManager;
    FirmwareUpdateManager* firmwareUpdateManager;
};
