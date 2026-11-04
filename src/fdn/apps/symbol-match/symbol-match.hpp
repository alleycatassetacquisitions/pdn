#pragma once

#include "state/state-machine.hpp"
#include "apps/symbol-match/symbol-match-states.hpp"
#include "apps/symbol-match/symbol-manager.hpp"

class SymbolWirelessManager;

class SymbolMatchApp : public StateMachine {
public:
    /** Symbol-match app: Selection → SymbolIdle → MatchSuccess → Selection; a refresh skips
     * MatchSuccess. */
    SymbolMatchApp(FDN* fdn, SymbolWirelessManager* symbolWirelessManager);
    /** Deletes the owned SymbolManager and clears non-owning pointers. */
    ~SymbolMatchApp();

    void populateStateMap() override;

private:
    RemoteDeviceCoordinator* remoteDeviceCoordinator;
    SymbolManager* symbolManager;
    SymbolWirelessManager* symbolWirelessManager;
};
