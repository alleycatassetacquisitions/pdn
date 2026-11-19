#pragma once

#include "state/state-machine.hpp"
#include "apps/player-fetch/band-registration.hpp"
#include "apps/player-fetch/player-fetch-states.hpp"
#include "apps/pdn-app-ids.hpp"
#include "game/player-provider.hpp"

class FirmwareUpdateManager;

/**
 * Registration app: the band waiting screen and the registration it leads to, the demo role
 * picker, and the firmware update it can hand off to.
 */
class PlayerFetchApp : public StateMachine {
public:
    /**
     * @p firmwareUpdateManager may be null to skip the hold-to-update state. @p bandRegistration
     * and @p remoteDeviceCoordinator are borrowed and must outlive the app.
     */
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
