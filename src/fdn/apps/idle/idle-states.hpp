#pragma once

#include <string>
#include "state/fdn-connect-state.hpp"
#include "state/state.hpp"
#include "device/fdn.hpp"
#include "wireless/remote-player-manager.hpp"
#include "alleycat-server/fdn-match-spool.hpp"
#include "alleycat-server/fdn-provisioning-manager.hpp"
#include "device/remote-device-coordinator.hpp"
#include "utils/simple-timer.hpp"

enum IdleStateId {
    IDLE = 0,
    PLAYER_DETECTED = 1,
    UPLOAD_PENDING = 2,
    REGISTRATION = 3,
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
    /** @return true once a PDN is Connected on either FDN jack. */
    bool transitionToCabledPdn();
    bool transitionToUploadPending();

private:
    RemotePlayerManager* remotePlayerManager;
    FdnMatchSpool* fdnMatchSpool;

    SimpleTimer uploadTimer;
    bool pdnCabled = false;

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
                        RemoteDeviceCoordinator* remoteDeviceCoordinator);
    ~PlayerDetectedState();

    void onStateMounted(FDN* fdn) override;
    void onStateLoop(FDN* fdn) override;
    void onStateDismounted(FDN* fdn) override;

    bool transitionToIdle();
    /** @return true once a PDN is Connected on either FDN jack. */
    bool transitionToCabledPdn();

private:
    RemotePlayerManager* remotePlayerManager;

    SimpleTimer playerDetectedTimer;
    bool pdnCabled = false;
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
    /** @copydoc TypedState::~TypedState */
    ~RegistrationState();

    /** Shows the waiting screen and listens for the lookup result. */
    void onStateMounted(FDN* fdn) override;
    /** Redraws when the lookup starts or finishes. */
    void onStateLoop(FDN* fdn) override;
    /** Drops the lookup listener. */
    void onStateDismounted(FDN* fdn) override;

    /** @return true on unplug, once a result has been shown, or once the PDN is provisioned. */
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
// UploadPendingState  (no connection required, so uses plain State)
// ---------------------------------------------------------------------------
class UploadPendingState : public TypedState<FDN> {
public:
    /** Uploads the staged match spool records over Wi-Fi. */
    explicit UploadPendingState(FdnMatchSpool* fdnMatchSpool);
    /** @copydoc TypedState::~TypedState */
    ~UploadPendingState();

    void onStateMounted(FDN* fdn) override;
    void onStateLoop(FDN* fdn) override;
    void onStateDismounted(FDN* fdn) override;

    bool transitionToIdle();

private:
    FdnMatchSpool* fdnMatchSpool;

    SimpleTimer glyphTimer;
    SimpleTimer fallbackTimer;
    SimpleTimer failureTimer;
    bool contentReady = false;
    bool showingFailure = false;
    bool uploadQueued = false;
    bool uploadDone = false;
    bool uploadFailed = false;

    static constexpr int FALLBACK_TIMEOUT_MS = 15000;
    static constexpr int FAILURE_DISPLAY_MS = 2000;
};
