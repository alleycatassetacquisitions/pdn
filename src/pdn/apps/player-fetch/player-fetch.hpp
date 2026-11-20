#pragma once

#include "state/state-machine.hpp"
#include "apps/player-fetch/band-registration.hpp"
#include "apps/player-fetch/player-fetch-states.hpp"
#include "apps/pdn-app-ids.hpp"
#include "game/player-provider.hpp"

class FirmwareUpdateManager;

/** Registration app: band waiting screen, registration, demo path and firmware update. */
class PlayerFetchApp : public StateMachine {
public:
    /** @p firmwareUpdateManager may be null to skip the hold-to-update state. */
    PlayerFetchApp(FirmwareUpdateManager* firmwareUpdateManager, PlayerProvider* playerProvider,
                   BandRegistration* bandRegistration,
                   RemoteDeviceCoordinator* remoteDeviceCoordinator, bool testIdentity);
    /** @copydoc StateMachine::~StateMachine */
    ~PlayerFetchApp() override;

    /** Registers PlayerFetch states and transitions. */
    void populateStateMap() override;

private:
    FirmwareUpdateManager* firmwareUpdateManager = nullptr;
    PlayerProvider* playerProvider = nullptr;
    BandRegistration* bandRegistration = nullptr;
    RemoteDeviceCoordinator* remoteDeviceCoordinator = nullptr;
    bool testIdentity = false;
};
