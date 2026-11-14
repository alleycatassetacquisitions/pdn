#pragma once

#include <string>
#include "state/fdn-connect-state.hpp"
#include "state/state.hpp"
#include "device/fdn.hpp"
#include "wireless/remote-player-manager.hpp"
#include "alleycat-server/fdn-provisioning-manager.hpp"
#include "alleycat-server/fdn-record-uploader.hpp"
#include "device/remote-device-coordinator.hpp"
#include "utils/simple-timer.hpp"

/** How often idle checks for match records and crash records to upload. */
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
    /** FDN home screen; also schedules the periodic uplink. */
    IdleState(RemotePlayerManager* remotePlayerManager,
              RemoteDeviceCoordinator* remoteDeviceCoordinator,
              FdnRecordUploader* fdnRecordUploader);

    void onStateMounted(FDN* fdn) override;
    void onStateDismounted(FDN* fdn) override;

    bool transitionToPlayerDetected();
    /**
     * @return true if a record is waiting to upload when the upload interval
     * expires; that call restarts it.
     */
    bool transitionToUploadPending();

private:
    RemotePlayerManager* remotePlayerManager;
    FdnRecordUploader* fdnRecordUploader;
    SimpleTimer uploadTimer;
};

// ---------------------------------------------------------------------------
// PlayerDetectedState
// ---------------------------------------------------------------------------
struct PlayerDetectionConfig {
    int timeout;
    uint8_t intensity;
};

class PlayerDetectedState : public FDNConnectState {
public:
    PlayerDetectedState(RemotePlayerManager* remotePlayerManager,
                        RemoteDeviceCoordinator* remoteDeviceCoordinator);

    void onStateMounted(FDN* fdn) override;
    void onStateDismounted(FDN* fdn) override;

    bool transitionToIdle();

private:
    RemotePlayerManager* remotePlayerManager;

    SimpleTimer playerDetectedTimer;

    static constexpr int STRONG_RSSI_THRESHOLD = -50;
    static constexpr int MEDIUM_RSSI_THRESHOLD = -65;

    static constexpr PlayerDetectionConfig WEAK_CFG = {2500, 80};
    static constexpr PlayerDetectionConfig MEDIUM_CFG = {2500, 150};
    static constexpr PlayerDetectionConfig STRONG_CFG = {5000, 255};
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
    enum class Screen { WAITING, LOOKING_UP, WELCOME, FAILED, KEY_NOT_ISSUED, SERVER_NOT_TRUSTED };

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
    /** Shows the periodic uplink while the uploader runs it. */
    explicit UploadPendingState(FdnRecordUploader* fdnRecordUploader);

    /** Shows the loading screen and starts an upload cycle for everything waiting. */
    void onStateMounted(FDN* fdn) override;
    /** Watches for the cycle's end and shows the failure screen if an upload in it failed. */
    void onStateLoop(FDN* fdn) override;
    /** Resets the upload phase; the uploader's sync() hands the radio back. */
    void onStateDismounted(FDN* fdn) override;

    /** @return true when no upload was attempted, it succeeded, or the failure screen ended. */
    bool transitionToIdle();

private:
    FdnRecordUploader* fdnRecordUploader;

    SimpleTimer glyphTimer;
    SimpleTimer failureTimer;
    enum class Phase : uint8_t { NOT_STARTED, UPLOADING, UPLOADED, SHOWING_FAILURE };
    Phase phase = Phase::NOT_STARTED;
    bool contentReady = false;

    static constexpr int FAILURE_DISPLAY_MS = 2000;
};
