#pragma once

#include "state/connect-state.hpp"
#include "device/device.hpp"
#include "device/remote-device-coordinator.hpp"
#include "device/drivers/serial-wrapper.hpp"
#include "utils/simple-timer.hpp"
#include "wireless/fdn-connect-wireless-manager.hpp"

class AuthDetectedState : public ConnectState {
public:
    explicit AuthDetectedState(RemoteDeviceCoordinator* remoteDeviceCoordinator, FDNConnectWirelessManager* fdnConnectWirelessManager);
    ~AuthDetectedState();

    void onStateMounted(Device* PDN) override;
    void onStateLoop(Device* PDN) override;
    void onStateDismounted(Device* PDN) override;

    bool isJackRequired(SerialIdentifier jack) override;
    bool transitionToIdle();
    bool transitionToMainMenu();

private:
    SimpleTimer glyphTimer;
    SimpleTimer switchTimer;
    bool contentReady = false;
    static constexpr int SWITCH_DELAY_MS = 5000;

    const char* AUTH_MESSAGE[2] = {"WELCOME", "ASSET #"};
    FDNConnectWirelessManager* fdnConnectWirelessManager;
};
