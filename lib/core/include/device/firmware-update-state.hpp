#pragma once

#include "state/state.hpp"

class Device;
class FirmwareUpdateManager;

class FirmwareUpdate : public TypedState<Device> {
public:
    /** Borrows firmwareUpdateManager; its owner pumps sync(). */
    FirmwareUpdate(int stateId, FirmwareUpdateManager* firmwareUpdateManager);

    /** Starts distributing this device's own running image. */
    void onStateMounted(Device* device) override;
    /** Returns to the host once the seed run ends or fails to start. */
    void onStateLoop(Device* device) override;
    /** Resets the transition flag; the seed run belongs to firmwareUpdateManager. */
    void onStateDismounted(Device* device) override;

    /** True once the seed run has ended. */
    bool transitionToHost() const;

private:
    FirmwareUpdateManager* firmwareUpdateManager;
    bool transitionToHostState = false;
};
