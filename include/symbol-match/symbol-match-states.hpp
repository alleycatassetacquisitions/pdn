#pragma once

#include "state/connect-state.hpp"
#include "state/state.hpp"
#include "symbol-match/symbol-manager.hpp"
#include "symbol-match/symbol-match.hpp"
#include "wireless/symbol-wireless-manager.hpp"

enum SymbolMatchStateId {
    SELECTION,
    SYMBOL_IDLE,
    LEFT_CONNECTED,
    RIGHT_CONNECTED,
    BOTH_CONNECTED,
    MATCH_SUCCESS,
};

class Selection : public State {
public:
    explicit Selection(SymbolManager* symbolManager);
    ~Selection();   
    void onStateMounted(Device *FDN) override;
    void onStateLoop(Device *FDN) override;
    void onStateDismounted(Device *FDN) override;
    bool transitionToIdle();

private:
    SymbolManager* symbolManager;
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
    bool transitionToLeftConnected();
    bool transitionToRightConnected();

    bool isPrimaryRequired() override;
    bool isAuxRequired() override;

private:
    void renderSymbolScreen(Device *FDN);
    void onSymbolMatchCommandReceived(SymbolMatchCommand command);

    bool transitionToSelectionState = false;
    bool transitionToLeftConnectedState = false;
    bool transitionToRightConnectedState = false;
 
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
class MatchSuccess : public State {
public:
    explicit MatchSuccess(SymbolManager* symbolManager);
    ~MatchSuccess();
    void onStateMounted(Device *FDN) override;
    void onStateLoop(Device *FDN) override;
    void onStateDismounted(Device *FDN) override;
    bool transitionToSelection();

private:
    SymbolManager* symbolManager;

    bool transitionToSelectionState = false;
};