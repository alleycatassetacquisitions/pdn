#pragma once

#include "state/state.hpp"

class Device;
class FirmwareUpdateManager;

/// Draws a static "UPDATING" label and waits out the seed run. Inherits State
/// rather than TypedState because it reads nothing beyond the accessors Device
/// itself provides, which lets the PDN and FDN host the same object.
class FirmwareUpdate : public State {
public:
    /// Does not own firmwareUpdateManager: the owner constructs it once and the
    /// platform loop pumps its sync(), since a device can be passively
    /// receiving without ever mounting this state. stateId is the hosting state
    /// machine's own id for this state, which differs per device build.
    FirmwareUpdate(int stateId, FirmwareUpdateManager* firmwareUpdateManager);

    /// Starts distributing this device's own running image.
    void onStateMounted(Device* device) override;
    /// Watches for the seed run to end, one way or another.
    void onStateLoop(Device* device) override;
    /// Resets the transition flag; the seed run itself is owned by
    /// firmwareUpdateManager, not this state, so there's nothing else to tear
    /// down here.
    void onStateDismounted(Device* device) override;

    /// True once beginSeeding's run is no longer streaming or repairing.
    bool transitionToIdle();

private:
    FirmwareUpdateManager* firmwareUpdateManager;
    bool transitionToIdleState = false;
};
