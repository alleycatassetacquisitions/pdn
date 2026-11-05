#pragma once

#include "state/state-machine.hpp"
#include "apps/player-fetch/player-fetch-states.hpp"
#include "apps/pdn-app-ids.hpp"
#include "device/wireless-manager.hpp"
#include "game/player-provider.hpp"

class FirmwareUpdateManager;

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

private:
    WirelessManager* wirelessManager = nullptr;
    FirmwareUpdateManager* firmwareUpdateManager = nullptr;
    PlayerProvider* playerProvider = nullptr;
};
