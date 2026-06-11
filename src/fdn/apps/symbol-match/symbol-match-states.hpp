#pragma once

#include "state/state.hpp"
#include "state/fdn-connect-state.hpp"
#include "apps/symbol-match/symbol-manager.hpp"
#include "device/fdn.hpp"
#include "utils/simple-timer.hpp"
#include "wireless/symbol-wireless-manager.hpp"
#include <string>

class HackedPlayersManager;
class FDNConnectWirelessManager;

enum SymbolMatchStateId {
    SELECTION,
    SYMBOL_IDLE,
    MATCH_SUCCESS,
    SYMBOL_MATCH_UPLOAD_PENDING,
    SYMBOL_MATCH_CONNECTION_DETECTED,
    SYMBOL_MATCH_AUTHORIZED_PDN,
    SYMBOL_MATCH_UNAUTHORIZED_PDN,
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
    bool transitionToMainMenu();

private:
    void renderSymbolScreen(FDN* fdn);
    void updateMatchSideLights(FDN* fdn, bool leftOn, bool rightOn);
    void syncMatchSideLights(FDN* fdn);

    SymbolManager* symbolManager;
    SymbolWirelessManager* symbolWirelessManager;
    FDN* mountedFdn = nullptr;

    bool transitionToSelectionState   = false;
    bool transitionToMainMenuApp      = false;
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
    MatchSuccess(SymbolManager* symbolManager,
                 RemoteDeviceCoordinator* remoteDeviceCoordinator,
                 SymbolWirelessManager* symbolWirelessManager);
    ~MatchSuccess();

    void onStateMounted(FDN* fdn) override;
    void onStateLoop(FDN* fdn) override;
    void onStateDismounted(FDN* fdn) override;

    bool transitionToMainMenu();

private:
    void renderSymbolScreen(FDN* fdn);

    SymbolManager* symbolManager;
    SymbolWirelessManager* symbolWirelessManager;
    bool toggleBlink = true;
    SimpleTimer bufferTimer;
    int bufferInterval = 3 * 1000;
    SimpleTimer renderTimer;
    int renderInterval = 200;
};

// ── SymbolMatchUploadPendingHacksState ────────────────────────────────────────
class SymbolMatchUploadPendingHacksState : public TypedState<FDN> {
public:
    explicit SymbolMatchUploadPendingHacksState(HackedPlayersManager* hackedPlayersManager);
    ~SymbolMatchUploadPendingHacksState();

    void onStateMounted(FDN* fdn) override;
    void onStateLoop(FDN* fdn) override;
    void onStateDismounted(FDN* fdn) override;

    bool transitionToIdle();

private:
    HackedPlayersManager* hackedPlayersManager;
    SimpleTimer glyphTimer;
    SimpleTimer fallbackTimer;
    bool contentReady  = false;
    int  pendingCount  = 0;
    int  completedCount = 0;
    static constexpr int FALLBACK_TIMEOUT_MS = 15000;
};

// ── SymbolMatchConnectionDetectedState ────────────────────────────────────────
class SymbolMatchConnectionDetectedState : public FDNConnectState {
public:
    SymbolMatchConnectionDetectedState(HackedPlayersManager* hackedPlayersManager,
                                       RemoteDeviceCoordinator* remoteDeviceCoordinator);
    ~SymbolMatchConnectionDetectedState();

    void onStateMounted(FDN* fdn) override;
    void onStateLoop(FDN* fdn) override;
    void onStateDismounted(FDN* fdn) override;

    void receivePdnConnection(const std::string& playerId);
    bool transitionToAuth();
    bool transitionToUnauthorized();
    bool transitionToSelection();

private:
    HackedPlayersManager* hackedPlayersManager;
    SimpleTimer glyphTimer;
    bool contentReady = false;
    bool wasHacked    = false;
    std::string pendingPlayerId;
};

// ── SymbolMatchAuthDetectedState ──────────────────────────────────────────────
class SymbolMatchAuthDetectedState : public FDNConnectState {
public:
    SymbolMatchAuthDetectedState(RemoteDeviceCoordinator* remoteDeviceCoordinator,
                                 FDNConnectWirelessManager* fdnConnectWirelessManager);
    ~SymbolMatchAuthDetectedState();

    void onStateMounted(FDN* fdn) override;
    void onStateLoop(FDN* fdn) override;
    void onStateDismounted(FDN* fdn) override;

    bool transitionToSelection();
    bool transitionToMainMenu();

private:
    FDNConnectWirelessManager* fdnConnectWirelessManager;
    SimpleTimer glyphTimer;
    SimpleTimer switchTimer;
    bool contentReady = false;
    static constexpr int SWITCH_DELAY_MS = 5000;
    const char* authMessage[2] = {"WELCOME", "ASSET #"};
};

// ── SymbolMatchUnauthorizedDetectedState ──────────────────────────────────────
class SymbolMatchUnauthorizedDetectedState : public FDNConnectState {
public:
    explicit SymbolMatchUnauthorizedDetectedState(RemoteDeviceCoordinator* remoteDeviceCoordinator);
    ~SymbolMatchUnauthorizedDetectedState();

    void onStateMounted(FDN* fdn) override;
    void onStateLoop(FDN* fdn) override;
    void onStateDismounted(FDN* fdn) override;

    bool transitionToSelection();
    bool transitionToHacking();

private:
    SimpleTimer glyphTimer;
    SimpleTimer switchTimer;
    bool contentReady = false;
    static constexpr int SWITCH_DELAY_MS = 5000;
    const char* accessDeniedMessage[2] = {"ACCESS", "DENIED"};
};
