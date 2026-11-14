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
    /**
     * Status screen while a registration is queued: the tap prompt naming the
     * pending jack, the lookup, and its result.
     */
    RegistrationState(FdnProvisioningManager* fdnProvisioningManager,
                      RemoteDeviceCoordinator* remoteDeviceCoordinator);
    /** Clears the lookup listener and drops the manager pointer. */
    ~RegistrationState();

    /** Shows the pending registration, lets registrations be pending, and listens for results. */
    void onStateMounted(FDN* fdn) override;
    /**
     * Follows the pending jack, the reader and the lookup, and redraws on a
     * change. A lookup's result stays up while the manager holds it.
     */
    void onStateLoop(FDN* fdn) override;
    /**
     * Drops the lookup listener, ends pending registrations (the queue waits),
     * and cancels a lookup still in flight.
     */
    void onStateDismounted(FDN* fdn) override;

    /**
     * @return true when no jack is connected, or when no lookup runs, no result
     * is on screen and no unprovisioned PDN is cabled.
     */
    bool transitionToIdle();

private:
    enum class Screen {
        // Mounted before the first pending registration was seen.
        WAITING,
        TAP_BAND,
        NO_READER,
        LOOKING_UP,
        // How the last lookup ended, drawn from lookupResult.
        RESULT,
    };

    void showPendingRegistration();
    void render(FDN* fdn);

    FdnProvisioningManager* fdnProvisioningManager;
    Screen screen = Screen::WAITING;
    // The jack the screen names: the pending one, or the one a result is for.
    SerialIdentifier screenJack = SerialIdentifier::INPUT_JACK;
    // Meaningful only on the RESULT screen.
    ProvisionLookupResult lookupResult = ProvisionLookupResult::LOOKUP_FAILED;
    bool displayIsDirty = false;
    std::string playerName;
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
