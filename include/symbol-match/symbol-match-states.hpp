#pragma once

#include "state/connect-state.hpp"
#include "state/state.hpp"
#include "symbol-match/symbol-manager.hpp"
#include "symbol-match/symbol-match.hpp"

enum SymbolMatchStateId {
    SELECTION,
    SYMBOL_IDLE,
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
    explicit SymbolIdle(SymbolManager* symbolManager, RemoteDeviceCoordinator* remoteDeviceCoordinator);
    ~SymbolIdle();
    void onStateMounted(Device *FDN) override;
    void onStateLoop(Device *FDN) override;
    void onStateDismounted(Device *FDN) override;
    bool transitionToSelection();

    bool isPrimaryRequired() override;
    bool isAuxRequired() override;

private:
    void renderSymbolScreen(Device *FDN);
    void renderSymbolGlyphs(Device *FDN);
    void renderTimer(Device *FDN);

    SimpleTimer refreshTimer;
    bool transitionToSelectionState = false;
    SymbolManager* symbolManager;
    int refreshInterval = (int)(5 * 1000);
    int lastTimeRendered = 0;
};