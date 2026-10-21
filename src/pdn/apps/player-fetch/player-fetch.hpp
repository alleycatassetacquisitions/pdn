#pragma once

#include "state/state-machine.hpp"
#include "apps/player-fetch/player-fetch-states.hpp"
#include "game/player.hpp"
#include "game/player-provider.hpp"
#include "device/wireless-manager.hpp"

class FirmwareUpdateManager;

constexpr int PLAYER_FETCH_APP_ID = 100;

/** Standalone registration app using the protobuf device player API. */
class PlayerFetchApp : public StateMachine {
public:
    /** @p firmwareUpdateManager may be null to disable OTA from pin entry. */
    PlayerFetchApp(Player* player, WirelessManager* wirelessManager,
                   FirmwareUpdateManager* firmwareUpdateManager);
    /** @copydoc StateMachine::~StateMachine */
    ~PlayerFetchApp() override;

    /** Registers PlayerFetch states and transitions. */
    void populateStateMap() override;

    /** True after the in-app awaken sequence completes. */
    bool readyForGameplay();

    /** Mutable registration profile for the current attempt. */
    PlayerProvider& getPlayerProvider() { return playerProvider; }

    /** Read-only registration profile for the current attempt. */
    const PlayerProvider& getPlayerProvider() const { return playerProvider; }

private:
    Player* player = nullptr;
    WirelessManager* wirelessManager = nullptr;
    FirmwareUpdateManager* firmwareUpdateManager = nullptr;
    PlayerProvider playerProvider;
};
