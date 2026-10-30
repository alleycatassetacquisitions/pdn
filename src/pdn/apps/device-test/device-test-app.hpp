#pragma once

#include "apps/device-test/device-test-states.hpp"
#include "state/state-machine.hpp"

class WirelessManager;

class DeviceTestApp : public StateMachine {
public:
    /** Operator-led hardware inspection flow for paired DUTs. */
    DeviceTestApp(RemoteDeviceCoordinator* remoteDeviceCoordinator,
                  DeviceTestWirelessManager* deviceTestWirelessManager,
                  WirelessManager* wirelessManager, int stateId);
    /** @copydoc StateMachine::~StateMachine */
    ~DeviceTestApp() override;

    /** @copydoc StateMachine::onStateMounted */
    void onStateMounted(Device* device) override;
    /** @copydoc StateMachine::onStateDismounted */
    void onStateDismounted(Device* device) override;

    /** Registers DeviceTest states and transitions. */
    void populateStateMap() override;

private:
    RemoteDeviceCoordinator* remoteDeviceCoordinator = nullptr;
    DeviceTestWirelessManager* deviceTestWirelessManager = nullptr;
    WirelessManager* wirelessManager = nullptr;
    DeviceTestSession session;
};
