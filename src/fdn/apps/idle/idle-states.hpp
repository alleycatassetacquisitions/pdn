#pragma once

#include <string>
#include "state/fdn-connect-state.hpp"
#include "state/state.hpp"
#include "device/fdn.hpp"
#include "wireless/remote-player-manager.hpp"
#include "alleycat-server/fdn-provisioning-manager.hpp"
#include "alleycat-server/fdn-match-spool.hpp"
#include "device/remote-device-coordinator.hpp"
#include "utils/simple-timer.hpp"

/** How often idle checks the match spool for an upload. */
constexpr unsigned long UPLOAD_CHECK_INTERVAL_MS = 5 * 60 * 1000;

enum IdleStateId {
    IDLE = 0,
    PLAYER_DETECTED = 1,
    REGISTRATION = 3,
    UPLOAD_PENDING = 2,
};

// ---------------------------------------------------------------------------
// IdleState
// ---------------------------------------------------------------------------
class IdleState : public FDNConnectState {
public:
    /** FDN home screen; also schedules match spool uploads. */
    IdleState(RemotePlayerManager* remotePlayerManager,
              RemoteDeviceCoordinator* remoteDeviceCoordinator, FdnMatchSpool* fdnMatchSpool);
    ~IdleState();

    void onStateMounted(FDN* fdn) override;
    void onStateLoop(FDN* fdn) override;
    void onStateDismounted(FDN* fdn) override;

    bool transitionToPlayerDetected();
    /** @return true once the upload timer has expired with matches staged. */
    bool transitionToUploadPending();

private:
    RemotePlayerManager* remotePlayerManager;
    FdnMatchSpool* fdnMatchSpool;
    SimpleTimer uploadTimer;
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

// ---------------------------------------------------------------------------
// RegistrationState
// ---------------------------------------------------------------------------
class RegistrationState : public FDNConnectState {
public:
    /** Status screen while an unprovisioned PDN is cabled: waiting, lookup, result. */
    RegistrationState(FdnProvisioningManager* fdnProvisioningManager,
                      RemoteDeviceCoordinator* remoteDeviceCoordinator);
    /** Clears the lookup listener and drops the manager pointer. */
    ~RegistrationState();

    /** Shows the waiting screen and listens for the lookup result. */
    void onStateMounted(FDN* fdn) override;
    /** Redraws when the lookup starts or finishes. */
    void onStateLoop(FDN* fdn) override;
    /** Drops the lookup listener. */
    void onStateDismounted(FDN* fdn) override;

    /** @return true when no jack is connected, once a result has been shown, or once no
     * unprovisioned PDN is cabled. */
    bool transitionToIdle();

private:
    enum class Screen { WAITING, LOOKING_UP, WELCOME, FAILED };

    void render(FDN* fdn);

    FdnProvisioningManager* fdnProvisioningManager;
    Screen screen = Screen::WAITING;
    bool displayIsDirty = false;
    std::string playerName;
    SimpleTimer resultTimer;

    static constexpr int RESULT_DISPLAY_MS = 3000;
};

// ---------------------------------------------------------------------------
// UploadPendingState  (no connection required, so it derives TypedState<FDN>)
// ---------------------------------------------------------------------------
class UploadPendingState : public TypedState<FDN> {
public:
    /** Uploads the staged match spool records over Wi-Fi. */
    explicit UploadPendingState(FdnMatchSpool* fdnMatchSpool);
    /** Non-owning; nothing to release. */
    ~UploadPendingState();

    /** Queues one upload batch and shows the loading screen. */
    void onStateMounted(FDN* fdn) override;
    /** Shows the failure screen on an error or the fallback timeout. */
    void onStateLoop(FDN* fdn) override;
    /** Returns the radio to ESP-NOW and resets the upload phase. */
    void onStateDismounted(FDN* fdn) override;

    /** @return true when no upload was attempted, it succeeded, or the failure screen ended. */
    bool transitionToIdle();

private:
    FdnMatchSpool* fdnMatchSpool;

    SimpleTimer glyphTimer;
    SimpleTimer fallbackTimer;
    SimpleTimer failureTimer;
    enum class Phase : uint8_t { NOT_STARTED, UPLOADING, UPLOADED, FAILED, SHOWING_FAILURE };
    Phase phase = Phase::NOT_STARTED;
    bool contentReady = false;

    static constexpr int FALLBACK_TIMEOUT_MS = 15000;
    static constexpr int FAILURE_DISPLAY_MS = 2000;
};
