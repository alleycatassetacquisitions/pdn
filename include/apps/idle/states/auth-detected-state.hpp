#pragma once

#include "state/connect-state.hpp"
#include "device/device.hpp"
#include "device/remote-device-coordinator.hpp"
#include "device/drivers/serial-wrapper.hpp"
#include "utils/simple-timer.hpp"

class AuthDetectedState : public ConnectState {
public:
    explicit AuthDetectedState(RemoteDeviceCoordinator* remoteDeviceCoordinator);
    ~AuthDetectedState();

    void onStateMounted(Device* PDN) override;
    void onStateLoop(Device* PDN) override;
    void onStateDismounted(Device* PDN) override;

    bool isJackRequired(SerialIdentifier jack) override;

private:
    SimpleTimer glyphTimer;
    SimpleTimer switchTimer;
    bool contentReady = false;
    static constexpr int SWITCH_DELAY_MS = 1000;

    const char* AUTH_MESSAGE[2] = {"AUTH", "SUCCESS"};
    const char* WELCOME_MESSAGE = "WELCOME";
};
