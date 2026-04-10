#pragma once

#include "device/device.hpp"
#include "state/state-machine.hpp"
#include "symbol-match-states.hpp"
#include "symbol-match/symbol-manager.hpp"

class SymbolWirelessManager;

class SymbolWirelessManager;

constexpr int SYMBOLMATCH_APP_ID = 1;

class SymbolMatch : public StateMachine {
public:
    SymbolMatch(Device* FDN, SymbolWirelessManager* symbolWirelessManager);
    ~SymbolMatch();

    void populateStateMap() override;

private:
    RemoteDeviceCoordinator* remoteDeviceCoordinator;
    SymbolManager* symbolManager;
    SymbolWirelessManager* symbolWirelessManager;
};