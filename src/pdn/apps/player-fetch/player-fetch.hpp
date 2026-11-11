#pragma once

#include "state/state-machine.hpp"
#include "apps/player-fetch/player-fetch-states.hpp"
#include "apps/pdn-app-ids.hpp"
#include "device/wireless-manager.hpp"
#include "game/player-provider.hpp"

class FirmwareUpdateManager;

/** Registration app: PIN entry, cabled-FDN lookup, welcome, and the demo role picker. */
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
    PdnProvisioningClient provisioningClient;
    FirmwareUpdateManager* firmwareUpdateManager = nullptr;
    PlayerProvider* playerProvider = nullptr;
};
