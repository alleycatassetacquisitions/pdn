#pragma once

#include "state/state-machine.hpp"
#include "apps/symbol-match/symbol-match-states.hpp"
#include "apps/symbol-match/symbol-manager.hpp"
#include "utils/simple-timer.hpp"
#include <string>

class SymbolWirelessManager;
class RemotePlayerManager;
class HackedPlayersManager;
class FDNConnectWirelessManager;

class SymbolMatch : public StateMachine {
public:
    SymbolMatch(FDN* fdn,
                SymbolWirelessManager* symbolWirelessManager,
                RemotePlayerManager* remotePlayerManager,
                HackedPlayersManager* hackedPlayersManager,
                FDNConnectWirelessManager* fdnConnectWirelessManager);
    ~SymbolMatch();

    void populateStateMap() override;

    void onStateMounted(Device* device) override;
    std::unique_ptr<Snapshot> onStatePaused(Device* device) override;
    void onStateResumed(Device* device, Snapshot* snapshot) override;
    void onStateDismounted(Device* device) override;
    void onStateLoop(Device* device) override;

private:
    bool shouldTransitionToUploadPending();
    void startIdleLightAnimation(FDN* fdn);
    void triggerNearbyDeviceAnimation(FDN* fdn, int rssi);
    void updateNearbyDeviceAnimation(FDN* fdn);

    RemoteDeviceCoordinator* remoteDeviceCoordinator;
    SymbolManager* symbolManager;
    SymbolWirelessManager* symbolWirelessManager;
    RemotePlayerManager* remotePlayerManager;
    HackedPlayersManager* hackedPlayersManager;
    FDNConnectWirelessManager* fdnConnectWirelessManager;

    bool connectionResolved = false;
    std::string pendingConnectedPlayerId;

    SimpleTimer uploadCheckTimer;
    SimpleTimer nearbyAnimationTimer;
    bool nearbyAnimationActive = false;

    static constexpr int UPLOAD_CHECK_INTERVAL_MS = 5 * 60 * 1000;
    static constexpr int kSelectionStateIndex          = 0;
    static constexpr int kUploadPendingStateIndex      = 3;
    static constexpr int kConnectionDetectedStateIndex = 4;
    static constexpr int STRONG_RSSI_THRESHOLD = -50;
    static constexpr int MEDIUM_RSSI_THRESHOLD = -60;
};
