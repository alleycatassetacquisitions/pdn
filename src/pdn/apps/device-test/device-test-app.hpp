#pragma once

#include "apps/device-test/device-test-states.hpp"
#include "state/state-machine.hpp"

class WirelessManager;

class DeviceTestApp : public StateMachine {
public:
    DeviceTestApp(RemoteDeviceCoordinator* remoteDeviceCoordinator,
                  DeviceTestWirelessManager* deviceTestWirelessManager,
                  WirelessManager* wirelessManager,
                  int stateId);
    ~DeviceTestApp() override;

    void onStateMounted(Device* device) override;
    void onStateDismounted(Device* device) override;

    void populateStateMap() override;

private:
    RemoteDeviceCoordinator* remoteDeviceCoordinator = nullptr;
    DeviceTestWirelessManager* deviceTestWirelessManager = nullptr;
    WirelessManager* wirelessManager = nullptr;
    DeviceTestSession session;
};
