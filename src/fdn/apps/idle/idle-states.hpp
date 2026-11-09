#pragma once

#include <string>
#include "state/fdn-connect-state.hpp"
#include "state/state.hpp"
#include "device/fdn.hpp"
#include "wireless/remote-player-manager.hpp"
#include "device/remote-device-coordinator.hpp"
#include "utils/simple-timer.hpp"

enum IdleStateId {
    IDLE = 0,
    PLAYER_DETECTED = 1,
};

// ---------------------------------------------------------------------------
// IdleState
// ---------------------------------------------------------------------------
class IdleState : public FDNConnectState {
public:
    /** FDN home screen. */
    IdleState(RemotePlayerManager* remotePlayerManager,
              RemoteDeviceCoordinator* remoteDeviceCoordinator);
    ~IdleState();

    void onStateMounted(FDN* fdn) override;
    void onStateLoop(FDN* fdn) override;
    void onStateDismounted(FDN* fdn) override;

    bool transitionToPlayerDetected();

private:
    RemotePlayerManager* remotePlayerManager;
};

// ---------------------------------------------------------------------------
// PlayerDetectedState
// ---------------------------------------------------------------------------
enum class PlayerDetectedStrength { WEAK, MEDIUM, STRONG };

struct PlayerDetectionConfig {
    PlayerDetectedStrength strength;
    int timeout;
    uint8_t intensity;
};

class PlayerDetectedState : public FDNConnectState {
public:
    PlayerDetectedState(RemotePlayerManager* remotePlayerManager,
                        RemoteDeviceCoordinator* remoteDeviceCoordinator);
    ~PlayerDetectedState();

    void onStateMounted(FDN* fdn) override;
    void onStateLoop(FDN* fdn) override;
    void onStateDismounted(FDN* fdn) override;

    bool transitionToIdle();

private:
    RemotePlayerManager* remotePlayerManager;

    SimpleTimer playerDetectedTimer;
    const PlayerDetectionConfig* activeConfig = nullptr;

    static constexpr int STRONG_RSSI_THRESHOLD = -50;
    static constexpr int MEDIUM_RSSI_THRESHOLD = -65;

    const PlayerDetectionConfig WEAK_CFG   = { PlayerDetectedStrength::WEAK,   2500,  80 };
    const PlayerDetectionConfig MEDIUM_CFG = { PlayerDetectedStrength::MEDIUM, 2500, 150 };
    const PlayerDetectionConfig STRONG_CFG = { PlayerDetectedStrength::STRONG, 5000, 255 };
};
