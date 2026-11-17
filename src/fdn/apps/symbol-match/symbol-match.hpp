#pragma once

#include "state/state-machine.hpp"
#include "apps/symbol-match/symbol-match-states.hpp"
#include "apps/symbol-match/symbol-manager.hpp"

class SymbolWirelessManager;

class SymbolMatch : public StateMachine {
public:
    /** Symbol-match app: Selection → SymbolIdle → MatchSuccess → Selection; a refresh skips
     * MatchSuccess. */
    SymbolMatch(FDN* fdn, SymbolWirelessManager* symbolWirelessManager);
    ~SymbolMatch();

    void populateStateMap() override;

private:
    RemoteDeviceCoordinator* remoteDeviceCoordinator;
    SymbolManager* symbolManager;
    SymbolWirelessManager* symbolWirelessManager;
};
