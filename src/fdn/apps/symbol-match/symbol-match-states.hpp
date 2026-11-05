#pragma once

#include "state/state.hpp"
#include "state/fdn-connect-state.hpp"
#include "apps/symbol-match/symbol-manager.hpp"
#include "device/fdn.hpp"
#include "utils/simple-timer.hpp"
#include "wireless/symbol-wireless-manager.hpp"

enum SymbolMatchStateId {
    SELECTION,
    SYMBOL_IDLE,
    MATCH_SUCCESS,
};

// ── Selection ────────────────────────────────────────────────────────────────
class Selection : public FDNConnectState {
public:
    Selection(SymbolManager* symbolManager,
              RemoteDeviceCoordinator* remoteDeviceCoordinator,
              SymbolWirelessManager* symbolWirelessManager);
    ~Selection();

    void onStateMounted(FDN* fdn) override;
    void onStateLoop(FDN* fdn) override;
    void onStateDismounted(FDN* fdn) override;

    bool transitionToIdle();

private:
    SymbolManager* symbolManager;
    SymbolWirelessManager* symbolWirelessManager;
    SimpleTimer bufferTimer;
    bool transitionToIdleState = false;
    int bufferInterval = 1 * 1000;
};

// ── SymbolIdle ───────────────────────────────────────────────────────────────
class SymbolIdle : public FDNConnectState {
public:
    SymbolIdle(SymbolManager* symbolManager,
               RemoteDeviceCoordinator* remoteDeviceCoordinator,
               SymbolWirelessManager* symbolWirelessManager);
    ~SymbolIdle();

    void onStateMounted(FDN* fdn) override;
    void onStateLoop(FDN* fdn) override;
    void onStateDismounted(FDN* fdn) override;

    bool transitionToSelection();
    bool transitionToMatchSuccess();

private:
    void renderSymbolScreen(FDN* fdn);
    void updateMatchSideLights(FDN* fdn, bool leftOn, bool rightOn);
    void syncMatchSideLights(FDN* fdn);

    SymbolManager* symbolManager;
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

// ── MatchSuccess ─────────────────────────────────────────────────────────────
class MatchSuccess : public FDNConnectState {
public:
    /** Shows FDN_CODEWORD and the win lights, and tells both PDNs the match was won. */
    MatchSuccess(RemoteDeviceCoordinator* remoteDeviceCoordinator,
                 SymbolWirelessManager* symbolWirelessManager);
    ~MatchSuccess();

    void onStateMounted(FDN* fdn) override;
    void onStateLoop(FDN* fdn) override;
    void onStateDismounted(FDN* fdn) override;

    /** True once the codeword has been on screen for CODEWORD_DURATION_MS. */
    bool transitionToSelection();

private:
    static constexpr int CODEWORD_DURATION_MS = 30 * 1000;

    SymbolWirelessManager* symbolWirelessManager;
    SimpleTimer codewordTimer;
    bool transitionToSelectionState = false;
};
