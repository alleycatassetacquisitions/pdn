#pragma once

#include "state/state.hpp"
#include "state/fdn-connect-state.hpp"
#include "apps/symbol-lock/symbol-manager.hpp"
#include "device/fdn.hpp"
#include "utils/simple-timer.hpp"
#include "wireless/symbol-wireless-manager.hpp"

enum SymbolLockStateId {
    SYMBOL_LOCK_SELECTION,
    SYMBOL_LOCK_IDLE,
    SYMBOL_LOCK_MATCH_SUCCESS,
};

class SymbolLockSelectionState : public FDNConnectState {
public:
    SymbolLockSelectionState(SymbolLockManager* symbolManager,
                           RemoteDeviceCoordinator* remoteDeviceCoordinator,
                           SymbolWirelessManager* symbolWirelessManager);
    ~SymbolLockSelectionState();

    void onStateMounted(FDN* fdn) override;
    void onStateLoop(FDN* fdn) override;
    void onStateDismounted(FDN* fdn) override;

    bool transitionToIdle();

private:
    SymbolLockManager* symbolManager;
    SymbolWirelessManager* symbolWirelessManager;
    SimpleTimer bufferTimer;
    bool transitionToIdleState = false;
    int bufferInterval = 1 * 1000;
};

class SymbolLockIdleState : public FDNConnectState {
public:
    SymbolLockIdleState(SymbolLockManager* symbolManager,
                        RemoteDeviceCoordinator* remoteDeviceCoordinator,
                        SymbolWirelessManager* symbolWirelessManager);
    ~SymbolLockIdleState();

    void onStateMounted(FDN* fdn) override;
    void onStateLoop(FDN* fdn) override;
    void onStateDismounted(FDN* fdn) override;

    bool transitionToSelection();
    bool transitionToMatchSuccess();

private:
    void renderSymbolScreen(FDN* fdn);
    void updateMatchSideLights(FDN* fdn, bool leftOn, bool rightOn);
    void syncMatchSideLights(FDN* fdn);
    bool symbolsSentToConnectedPorts() const;

    SymbolLockManager* symbolManager;
    SymbolWirelessManager* symbolWirelessManager;
    FDN* mountedFdn = nullptr;

    bool transitionToSelectionState = false;
    bool leftConnected  = false;
    bool rightConnected = false;
    bool lastSideLightLeft_  = false;
    bool lastSideLightRight_ = false;
    bool blinkToggle   = true;
    bool symbolSentLeft  = false;
    bool symbolSentRight = false;
    int  lastTimeRendered = 0;
};

class SymbolLockMatchSuccessState : public TypedState<FDN> {
public:
    SymbolLockMatchSuccessState(SymbolLockManager* symbolManager,
                                SymbolWirelessManager* symbolWirelessManager,
                                RemoteDeviceCoordinator* remoteDeviceCoordinator);
    ~SymbolLockMatchSuccessState();

    void onStateMounted(FDN* fdn) override;
    void onStateLoop(FDN* fdn) override;
    void onStateDismounted(FDN* fdn) override;

    bool transitionToDemoModule();

private:
    void renderSymbolScreen(FDN* fdn);

    SymbolLockManager* symbolManager;
    SymbolWirelessManager* symbolWirelessManager;
    RemoteDeviceCoordinator* remoteDeviceCoordinator;
    bool toggleBlink = true;
    bool demoTransitionReady = false;
    SimpleTimer bufferTimer;
    int bufferInterval = 3 * 1000;
    SimpleTimer renderTimer;
    int renderInterval = 200;
};
