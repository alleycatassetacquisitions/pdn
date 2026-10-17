#pragma once

#include "state/state.hpp"

class Device;
class FirmwareUpdateManager;

/** Draws a static "UPDATING" label and waits out the seed run. */
class FirmwareUpdate : public TypedState<Device> {
public:
    /** stateId is the host state machine's id for this state. Borrows firmwareUpdateManager; its owner pumps sync(). */
    FirmwareUpdate(int stateId, FirmwareUpdateManager* firmwareUpdateManager);

    /** Starts distributing this device's own running image. */
    void onStateMounted(Device* device) override;
    /** Watches for the seed run to end, one way or another. */
    void onStateLoop(Device* device) override;
    /** Resets the transition flag; the seed run belongs to firmwareUpdateManager. */
    void onStateDismounted(Device* device) override;

    /** True once the seed's run has ended; the host returns to the screen the run started from. */
    bool transitionToHost() const;

private:
    FirmwareUpdateManager* firmwareUpdateManager;
    bool transitionToHostState = false;
};
