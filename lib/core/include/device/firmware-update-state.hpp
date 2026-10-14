#pragma once

#include "state/state.hpp"

class Device;
class FirmwareUpdateManager;

/**
 * How long the secondary button is held, on a screen that hosts FirmwareUpdate,
 * before the device starts seeding its own image.
 */
constexpr unsigned long FIRMWARE_UPDATE_HOLD_MS = 5000;

/**
 * The seed gesture: the secondary button held FIRMWARE_UPDATE_HOLD_MS. A hold
 * that is already a long press when the host screen mounts (through power-on,
 * or back from a run that ended at once) does not count until it is let go.
 */
class FirmwareUpdateHold {
public:
    /** Call every pass of the host's onStateLoop; true once the gesture completes. */
    bool poll(Device* device);
    /** Call from the host's onStateDismounted. */
    void reset();

private:
    bool releasedSinceMount = false;
};

/**
 * Draws a static "UPDATING" label and waits out the seed run. Inherits State
 * rather than TypedState because it reads nothing beyond the accessors Device
 * itself provides, which lets the PDN and FDN host the same class.
 */
class FirmwareUpdate : public State {
public:
    /**
     * stateId is the host state machine's id for this state. Borrows
     * firmwareUpdateManager, whose sync() the platform loop pumps.
     */
    FirmwareUpdate(int stateId, FirmwareUpdateManager* firmwareUpdateManager);

    /** Starts distributing this device's own running image. */
    void onStateMounted(Device* device) override;
    /** Watches for the seed run to end, one way or another. */
    void onStateLoop(Device* device) override;
    /** Resets the transition flag; the seed run belongs to firmwareUpdateManager. */
    void onStateDismounted(Device* device) override;

    /**
     * True once the seed's run has ended (isSeeding() is false); the host
     * returns to the screen the run started from.
     */
    bool transitionToIdle();

private:
    FirmwareUpdateManager* firmwareUpdateManager;
    bool transitionToIdleState = false;
};
