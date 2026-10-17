#pragma once

#include <functional>
#include <string>
#include "state/fdn-connect-state.hpp"
#include "state/state.hpp"
#include "device/fdn.hpp"
#include "wireless/remote-player-manager.hpp"
#include "wireless/fdn-connect-wireless-manager.hpp"
#include "apps/hacking/hacked-players-manager.hpp"
#include "device/remote-device-coordinator.hpp"
#include "utils/simple-timer.hpp"

enum IdleStateId {
    IDLE                = 0,
    PLAYER_DETECTED     = 1,
    AUTHORIZED_PDN      = 2,
    UNAUTHORIZED_PDN    = 3,
    CONNECTION_DETECTED = 4,
    UPLOAD_PENDING      = 5,
};

// ---------------------------------------------------------------------------
// IdleState
// ---------------------------------------------------------------------------
class IdleState : public FDNConnectState {
public:
    IdleState(RemotePlayerManager* remotePlayerManager,
              HackedPlayersManager* hackedPlayersManager,
              FDNConnectWirelessManager* fdnConnectWirelessManager,
              RemoteDeviceCoordinator* remoteDeviceCoordinator);
    ~IdleState();

    void onStateMounted(FDN* fdn) override;
    void onStateLoop(FDN* fdn) override;
    void onStateDismounted(FDN* fdn) override;

    void setConnectionHandler(std::function<void(const std::string&, const uint8_t*)> handler);

    bool transitionToPlayerDetected();
    bool transitionToConnectionDetected();
    bool transitionToUploadPending();

private:
    RemotePlayerManager* remotePlayerManager;
    HackedPlayersManager* hackedPlayersManager;
    FDNConnectWirelessManager* fdnConnectWirelessManager;

    std::function<void(const std::string&, const uint8_t*)> connectionHandler;

    SimpleTimer uploadTimer;
    bool connectionResolved = false;
    bool wasConnected       = false;

    static constexpr int UPLOAD_CHECK_INTERVAL_MS = 5 * 60 * 1000;
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
                        HackedPlayersManager* hackedPlayersManager,
                        FDNConnectWirelessManager* fdnConnectWirelessManager,
                        RemoteDeviceCoordinator* remoteDeviceCoordinator);
    ~PlayerDetectedState();

    void onStateMounted(FDN* fdn) override;
    void onStateLoop(FDN* fdn) override;
    void onStateDismounted(FDN* fdn) override;

    void setConnectionHandler(std::function<void(const std::string&, const uint8_t*)> handler);

    bool transitionToIdle();
    bool transitionToConnectionDetected();

private:
    RemotePlayerManager* remotePlayerManager;
    HackedPlayersManager* hackedPlayersManager;
    FDNConnectWirelessManager* fdnConnectWirelessManager;

    std::function<void(const std::string&, const uint8_t*)> connectionHandler;

    SimpleTimer playerDetectedTimer;
    bool connectionResolved = false;
    bool wasConnected       = false;
    const PlayerDetectionConfig* activeConfig = nullptr;

    static constexpr int STRONG_RSSI_THRESHOLD = -50;
    static constexpr int MEDIUM_RSSI_THRESHOLD = -65;

    const PlayerDetectionConfig WEAK_CFG   = { PlayerDetectedStrength::WEAK,   2500,  80 };
    const PlayerDetectionConfig MEDIUM_CFG = { PlayerDetectedStrength::MEDIUM, 2500, 150 };
    const PlayerDetectionConfig STRONG_CFG = { PlayerDetectedStrength::STRONG, 5000, 255 };
};

// ---------------------------------------------------------------------------
// ConnectionDetectedState
// ---------------------------------------------------------------------------
class ConnectionDetectedState : public FDNConnectState {
public:
    ConnectionDetectedState(HackedPlayersManager* hackedPlayersManager,
                            FDNConnectWirelessManager* fdnConnectWirelessManager,
                            RemoteDeviceCoordinator* remoteDeviceCoordinator);
    ~ConnectionDetectedState();

    void onStateMounted(FDN* fdn) override;
    void onStateLoop(FDN* fdn) override;
    void onStateDismounted(FDN* fdn) override;

    void receivePdnConnection(const std::string& playerId);

    bool transitionToAuth();
    bool transitionToUnauthorized();
    bool transitionToIdle();

private:
    HackedPlayersManager* hackedPlayersManager;
    FDNConnectWirelessManager* fdnConnectWirelessManager;

    SimpleTimer glyphTimer;
    bool contentReady = false;
    bool wasHacked    = false;
    std::string pendingPlayerId;
};

// ---------------------------------------------------------------------------
// AuthDetectedState
// ---------------------------------------------------------------------------
class AuthDetectedState : public FDNConnectState {
public:
    AuthDetectedState(RemoteDeviceCoordinator* remoteDeviceCoordinator,
                      FDNConnectWirelessManager* fdnConnectWirelessManager);
    ~AuthDetectedState();

    void onStateMounted(FDN* fdn) override;
    void onStateLoop(FDN* fdn) override;
    void onStateDismounted(FDN* fdn) override;

    bool transitionToIdle();
    bool transitionToMainMenu();

private:
    FDNConnectWirelessManager* fdnConnectWirelessManager;

    SimpleTimer glyphTimer;
    SimpleTimer switchTimer;
    bool contentReady = false;

    static constexpr int SWITCH_DELAY_MS = 5000;
    const char* AUTH_MESSAGE[2] = {"WELCOME", "ASSET #"};
};

// ---------------------------------------------------------------------------
// UnauthorizedDetectedState
// ---------------------------------------------------------------------------
class UnauthorizedDetectedState : public FDNConnectState {
public:
    explicit UnauthorizedDetectedState(RemoteDeviceCoordinator* remoteDeviceCoordinator);
    ~UnauthorizedDetectedState();

    void onStateMounted(FDN* fdn) override;
    void onStateLoop(FDN* fdn) override;
    void onStateDismounted(FDN* fdn) override;

    bool transitionToIdle();
    bool transitionToHacking();

private:
    SimpleTimer glyphTimer;
    SimpleTimer switchTimer;
    bool contentReady = false;

    static constexpr int SWITCH_DELAY_MS = 5000;
    const char* ACCESS_DENIED_MESSAGE[2] = {"ACCESS", "DENIED"};
};

// ---------------------------------------------------------------------------
// UploadPendingHacksState  (no connection required, so uses plain State)
// ---------------------------------------------------------------------------
class UploadPendingHacksState : public TypedState<FDN> {
public:
    explicit UploadPendingHacksState(HackedPlayersManager* hackedPlayersManager);
    ~UploadPendingHacksState();

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
