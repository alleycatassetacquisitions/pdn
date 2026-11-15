#pragma once

#include "state/state-machine.hpp"
#include "apps/player-fetch/player-fetch-states.hpp"
#include "apps/pdn-app-ids.hpp"
#include "game/player-provider.hpp"

class FirmwareUpdateManager;

/** Registration app: the role picker, and the firmware update it can hand off to. */
class PlayerFetchApp : public StateMachine {
public:
    /** @p firmwareUpdateManager may be null to skip the hold-to-update state. */
    PlayerFetchApp(FirmwareUpdateManager* firmwareUpdateManager, PlayerProvider* playerProvider,
                   bool testIdentity);
    /** @copydoc StateMachine::~StateMachine */
    ~PlayerFetchApp() override;

    /** Registers PlayerFetch states and transitions. */
    void populateStateMap() override;

private:
    FirmwareUpdateManager* firmwareUpdateManager = nullptr;
    PlayerProvider* playerProvider = nullptr;
    bool testIdentity = false;
};
