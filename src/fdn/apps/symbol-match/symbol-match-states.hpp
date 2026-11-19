#pragma once

#include "state/state.hpp"
#include "alleycat-server/fdn-provisioning-manager.hpp"
#include "state/fdn-connect-state.hpp"
#include "apps/symbol-match/symbol-manager.hpp"
#include "device/fdn.hpp"
#include "utils/simple-timer.hpp"
#include "wireless/symbol-wireless-manager.hpp"

enum SymbolMatchStateId {
    SELECTION,
    SYMBOL_IDLE,
    MATCH_SUCCESS,
    REGISTRATION,
};

// ── Selection ────────────────────────────────────────────────────────────────
class Selection : public FDNConnectState {
public:
    Selection(SymbolManager* symbolManager, RemoteDeviceCoordinator* remoteDeviceCoordinator,
              SymbolWirelessManager* symbolWirelessManager,
              FdnProvisioningManager* fdnProvisioningManager);
    ~Selection();

    void onStateMounted(FDN* fdn) override;
    void onStateLoop(FDN* fdn) override;
    void onStateDismounted(FDN* fdn) override;

    bool transitionToIdle();
    /** True once a PDN waits to register: registration takes the screen. */
    bool transitionToRegistration();

private:
    SymbolManager* symbolManager;
    SymbolWirelessManager* symbolWirelessManager;
    FdnProvisioningManager* fdnProvisioningManager;
    SimpleTimer bufferTimer;
    bool transitionToIdleState = false;
    bool transitionToRegistrationState = false;
    int bufferInterval = 1 * 1000;
};

// ── SymbolIdle ───────────────────────────────────────────────────────────────
class SymbolIdle : public FDNConnectState {
public:
    SymbolIdle(SymbolManager* symbolManager, RemoteDeviceCoordinator* remoteDeviceCoordinator,
               SymbolWirelessManager* symbolWirelessManager,
               FdnProvisioningManager* fdnProvisioningManager);
    ~SymbolIdle();

    void onStateMounted(FDN* fdn) override;
    void onStateLoop(FDN* fdn) override;
    void onStateDismounted(FDN* fdn) override;

    bool transitionToSelection();
    bool transitionToMatchSuccess();
    /** True once a PDN waits to register: registration takes the screen. */
    bool transitionToRegistration();

private:
    void renderSymbolScreen(FDN* fdn);
    void updateMatchSideLights(FDN* fdn, bool leftOn, bool rightOn);
    void syncMatchSideLights(FDN* fdn);

    SymbolManager* symbolManager;
    SymbolWirelessManager* symbolWirelessManager;
    FdnProvisioningManager* fdnProvisioningManager;
    FDN* mountedFdn = nullptr;

    bool transitionToSelectionState = false;
    bool transitionToRegistrationState = false;
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

// ── Registration ─────────────────────────────────────────────────────────────
class Registration : public TypedState<FDN> {
public:
    /** Shows the band registration the manager runs while PDNs wait to register. */
    explicit Registration(FdnProvisioningManager* fdnProvisioningManager);
    /** Drops the manager pointer; dismount already let go of its listener. */
    ~Registration() override;

    /** Lets turns open and follows the manager's view. */
    void onStateMounted(FDN* fdn) override;
    /** Redraws after the view changed. */
    void onStateLoop(FDN* fdn) override;
    /** Closes turns and stops following the view. */
    void onStateDismounted(FDN* fdn) override;

    /** True once nobody waits, no lookup runs and no result holds. */
    bool transitionToSelection();

private:
    FdnProvisioningManager* fdnProvisioningManager;
    RegistrationView view;
    bool displayIsDirty = false;
    bool transitionToSelectionState = false;
};
