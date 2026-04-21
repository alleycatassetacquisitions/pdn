#pragma once

#include "state/connect-state.hpp"
#include "state/state.hpp"
#include "symbol-match/symbol-manager.hpp"
#include "utils/simple-timer.hpp"
#include "wireless/symbol-wireless-manager.hpp"

enum SymbolMatchStateId {
    SELECTION,
    SYMBOL_IDLE,
    MATCH_SUCCESS,
};

class Selection : public ConnectState {
public:
    explicit Selection(SymbolManager* symbolManager, RemoteDeviceCoordinator* remoteDeviceCoordinator,
                        SymbolWirelessManager* symbolWirelessManager);
    ~Selection();   
    void onStateMounted(Device *FDN) override;
    void onStateLoop(Device *FDN) override;
    void onStateDismounted(Device *FDN) override;
    bool transitionToIdle();
    bool isPrimaryRequired() override;
    bool isAuxRequired() override;

private:
    SymbolManager* symbolManager;
    SymbolWirelessManager* symbolWirelessManager;
    SimpleTimer bufferTimer;
    bool transitionToIdleState = false;
    int bufferInterval = 1 * 1000;
};

class SymbolIdle : public ConnectState {
public:
    explicit SymbolIdle(SymbolManager* symbolManager, RemoteDeviceCoordinator* remoteDeviceCoordinator,
                        SymbolWirelessManager* symbolWirelessManager);
    ~SymbolIdle();
    void onStateMounted(Device *FDN) override;
    void onStateLoop(Device *FDN) override;
    void onStateDismounted(Device *FDN) override;
    bool transitionToSelection();
    bool transitionToMatchSuccess();

    bool isPrimaryRequired() override;
    bool isAuxRequired() override;

private:
    void renderSymbolScreen(Device *FDN);
    void onSymbolMatchCommandReceived(SymbolMatchCommand command);

    bool transitionToSelectionState = false;
    bool transitionToMatchSuccessState = false;
 
    SymbolManager* symbolManager;
    SymbolWirelessManager* symbolWirelessManager;
    /// Set while SYMBOL_IDLE is active; used when ESP-NOW callbacks need the display.
    Device* mountedFdn = nullptr;
    int lastTimeRendered = 0;

    bool leftConnected = false;
    bool rightConnected = false;
    bool blinkToggle = true;
    bool symbolSentLeft = false;
    bool symbolSentRight = false;
};

class MatchSuccess : public ConnectState {
public:
    explicit MatchSuccess(SymbolManager* symbolManager, RemoteDeviceCoordinator* remoteDeviceCoordinator,
        SymbolWirelessManager* symbolWirelessManager);
    ~MatchSuccess();
    void onStateMounted(Device *FDN) override;
    void onStateLoop(Device *FDN) override;
    void onStateDismounted(Device *FDN) override;
    bool transitionToSelection();

    bool isPrimaryRequired() override;
    bool isAuxRequired() override;

private:
    SymbolManager* symbolManager;
    SymbolWirelessManager* symbolWirelessManager;

    void renderSymbolScreen(Device *FDN);

    bool transitionToSelectionState = false;
    bool toggleBlink = true;

    SimpleTimer bufferTimer;
    int bufferInterval = 3 * 1000;

    SimpleTimer renderTimer;
    int renderInterval = 0.2 * 1000;
};
