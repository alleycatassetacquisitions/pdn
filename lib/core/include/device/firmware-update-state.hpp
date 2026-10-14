#pragma once

#include "state/state.hpp"

class Device;
class FirmwareUpdateManager;

/// How long the secondary button is held, on a screen that hosts FirmwareUpdate,
/// before the device starts seeding its own image.
constexpr unsigned long FIRMWARE_UPDATE_HOLD_MS = 5000;

/**
 * The seed gesture: the secondary button held FIRMWARE_UPDATE_HOLD_MS. Counts
 * only a hold that began on the host screen, so a hold carried in (through
 * power-on, or back from a run that ended at once) does not fire again until
 * the button has been let go.
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

/// Draws a static "UPDATING" label and waits out the seed run. Inherits State
/// rather than TypedState because it reads nothing beyond the accessors Device
/// itself provides, which lets the PDN and FDN host the same object.
class FirmwareUpdate : public State {
public:
    /// Does not own firmwareUpdateManager: the owner constructs it once and the
    /// platform loop pumps its sync(), since a device can be passively
    /// receiving without ever mounting this state. stateId is the hosting state
    /// machine's own id for this state.
    FirmwareUpdate(int stateId, FirmwareUpdateManager* firmwareUpdateManager);

    /// Starts distributing this device's own running image.
    void onStateMounted(Device* device) override;
    /// Watches for the seed run to end, one way or another.
    void onStateLoop(Device* device) override;
    /// Resets the transition flag; the seed run itself is owned by
    /// firmwareUpdateManager, not this state, so there's nothing else to tear
    /// down here.
    void onStateDismounted(Device* device) override;

    /// True once the seed's run has ended (isSeeding() is false); the host
    /// returns to the screen the run started from.
    bool transitionToIdle();

private:
    FirmwareUpdateManager* firmwareUpdateManager;
    bool transitionToIdleState = false;
};
