#pragma once

#include "state/state-machine.hpp"
#include "apps/symbol-match/symbol-match-states.hpp"
#include "apps/symbol-match/symbol-manager.hpp"

class SymbolWirelessManager;

class SymbolMatchApp : public StateMachine {
public:
    /**
     * Symbol-match app: Selection → SymbolIdle → MatchSuccess → Selection; a refresh skips
     * MatchSuccess. Selection and SymbolIdle give way to Registration while a PDN waits to
     * register, and Registration returns to Selection once nobody does.
     */
    SymbolMatchApp(RemoteDeviceCoordinator* remoteDeviceCoordinator,
                   SymbolWirelessManager* symbolWirelessManager,
                   FdnProvisioningManager* fdnProvisioningManager);
    /** Deletes the owned SymbolManager. */
    ~SymbolMatchApp();

    void populateStateMap() override;

private:
    RemoteDeviceCoordinator* remoteDeviceCoordinator;
    SymbolManager* symbolManager;
    SymbolWirelessManager* symbolWirelessManager;
    FdnProvisioningManager* fdnProvisioningManager;
};
