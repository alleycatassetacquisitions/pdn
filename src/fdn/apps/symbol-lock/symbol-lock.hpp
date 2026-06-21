#pragma once

#include "state/typed-state-machine.hpp"
#include "apps/symbol-lock/symbol-lock-states.hpp"
#include "apps/symbol-lock/symbol-manager.hpp"
#include "utils/simple-timer.hpp"
#include "device/fdn.hpp"

class SymbolWirelessManager;
class RemotePlayerManager;

class SymbolLock : public TypedStateMachine<FDN> {
public:
    SymbolLock(FDN* fdn,
               SymbolWirelessManager* symbolWirelessManager,
               RemotePlayerManager* remotePlayerManager,
               bool singleSymbolMode);
    ~SymbolLock();

    void populateStateMap() override;

    void onStateMounted(Device* device) override;
    void onStateDismounted(Device* device) override;
    void onStateLoop(Device* device) override;

private:
    void startIdleLightAnimation(FDN* fdn);
    void triggerNearbyDeviceAnimation(FDN* fdn, int rssi);
    void updateNearbyDeviceAnimation(FDN* fdn);

    RemoteDeviceCoordinator* remoteDeviceCoordinator;
    SymbolLockManager* symbolManager;
    SymbolWirelessManager* symbolWirelessManager;
    RemotePlayerManager* remotePlayerManager;

    SimpleTimer nearbyAnimationTimer;
    bool nearbyAnimationActive = false;

    static constexpr int kSelectionStateIndex   = 0;
    static constexpr int kIdleStateIndex        = 1;
    static constexpr int kMatchSuccessStateIndex = 2;
    static constexpr int STRONG_RSSI_THRESHOLD = -50;
    static constexpr int MEDIUM_RSSI_THRESHOLD = -60;
};
