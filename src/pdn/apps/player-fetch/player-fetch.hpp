#pragma once

#include "state/state-machine.hpp"
#include "apps/player-fetch/player-fetch-states.hpp"
#include "device/wireless-manager.hpp"
#include "game/player-provider.hpp"

class FirmwareUpdateManager;

constexpr int PLAYER_FETCH_APP_ID = 100;

/** Standalone registration app using the protobuf device player API. */
class PlayerFetchApp : public StateMachine {
public:
    /** @p firmwareUpdateManager may be null to skip the hold-to-update state. */
    PlayerFetchApp(WirelessManager* wirelessManager, FirmwareUpdateManager* firmwareUpdateManager,
                   PlayerProvider* playerProvider);
    /** @copydoc StateMachine::~StateMachine */
    ~PlayerFetchApp() override;

    /** Registers PlayerFetch states and transitions. */
    void populateStateMap() override;

    /** True after the in-app awaken sequence completes. */
    bool readyForGameplay();

private:
    WirelessManager* wirelessManager = nullptr;
    FirmwareUpdateManager* firmwareUpdateManager = nullptr;
    PlayerProvider* playerProvider = nullptr;
};
