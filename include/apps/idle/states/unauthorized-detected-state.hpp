#pragma once

#include "state/connect-state.hpp"
#include "device/device.hpp"
#include "device/remote-device-coordinator.hpp"
#include "device/drivers/serial-wrapper.hpp"
#include "utils/simple-timer.hpp"

class UnauthorizedDetectedState : public ConnectState {
public:
    explicit UnauthorizedDetectedState(RemoteDeviceCoordinator* remoteDeviceCoordinator);
    ~UnauthorizedDetectedState();

    void onStateMounted(Device* PDN) override;
    void onStateLoop(Device* PDN) override;
    void onStateDismounted(Device* PDN) override;

    bool isJackRequired(SerialIdentifier jack) override;

private:
    SimpleTimer glyphTimer;
    SimpleTimer switchTimer;
    bool contentReady = false;
    static constexpr int SWITCH_DELAY_MS = 500;

    const char* UNAUTHORIZED_MESSAGE[2] = {"UNAUTHORIZED", "PDN"};
    const char* ACCESS_DENIED_MESSAGE[2] = {"ACCESS", "DENIED"};
};
