#pragma once

#include "state/connect-state.hpp"
#include "state/state.hpp"
#include "symbol-match/symbol-manager.hpp"
#include "symbol-match/symbol-match.hpp"

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
    explicit SymbolIdle(SymbolManager* symbolManager, RemoteDeviceCoordinator* remoteDeviceCoordinator);
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

    bool transitionToSelectionState = false;
    bool transitionToLeftConnectedState = false;
    bool transitionToRightConnectedState = false;
 
    SymbolManager* symbolManager;
    int lastTimeRendered = 0;
};

class LeftConnected : public ConnectState {
public:
    explicit LeftConnected(SymbolManager* symbolManager, RemoteDeviceCoordinator* remoteDeviceCoordinator);
    ~LeftConnected();
    void onStateMounted(Device *FDN) override;
    void onStateLoop(Device *FDN) override;
    void onStateDismounted(Device *FDN) override;
    bool transitionToSymbolIdle();
    bool transitionToBothConnected();
    bool transitionToSelection();

    bool isPrimaryRequired() override;
    bool isAuxRequired() override;

private:
    void renderSymbolScreen(Device *FDN);

    SymbolManager* symbolManager;

    bool transitionToSelectionState = false;
    bool transitionToSymbolIdleState = false;
    bool transitionToBothConnectedState = false;

    int lastTimeRendered = 0;
    bool toggleBlink = false;
};

class RightConnected : public ConnectState {
public:
    explicit RightConnected(SymbolManager* symbolManager, RemoteDeviceCoordinator* remoteDeviceCoordinator);
    ~RightConnected();
    void onStateMounted(Device *FDN) override;
    void onStateLoop(Device *FDN) override;
    void onStateDismounted(Device *FDN) override;
    bool transitionToSymbolIdle();
    bool transitionToBothConnected();
    bool transitionToSelection();

    bool isPrimaryRequired() override;
    bool isAuxRequired() override;

private:
    SymbolManager* symbolManager;
    void renderSymbolScreen(Device *FDN);
    bool transitionToSelectionState = false;
    bool transitionToSymbolIdleState = false;
    bool transitionToBothConnectedState = false;

    int lastTimeRendered = 0;
    bool toggleBlink = false;
    int debounce = 0;
};

class BothConnected : public ConnectState {
public:
    explicit BothConnected(SymbolManager* symbolManager, RemoteDeviceCoordinator* remoteDeviceCoordinator);
    ~BothConnected();
    void onStateMounted(Device *FDN) override;
    void onStateLoop(Device *FDN) override;
    void onStateDismounted(Device *FDN) override;
    bool transitionToSelection();
    bool transitionToMatchSuccess();
    bool transitionToLeftConnected();
    bool transitionToRightConnected();

    bool isPrimaryRequired() override;
    bool isAuxRequired() override;

private:
    SymbolManager* symbolManager;
    void renderSymbolScreen(Device *FDN);
    bool transitionToSelectionState = false;
    bool transitionToLeftConnectedState = false;
    bool transitionToRightConnectedState = false;
    bool transitionToMatchSuccessState = false;

    int lastTimeRendered = 0;
    bool toggleBlink = false;
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