#pragma once

#include <functional>
#include <string>
#include "state/connect-state.hpp"
#include "device/device.hpp"
#include "device/remote-device-coordinator.hpp"
#include "device/drivers/serial-wrapper.hpp"
#include "apps/idle/idle-resources.hpp"
#include "wireless/remote-player-manager.hpp"
#include "wireless/fdn-connect-wireless-manager.hpp"
#include "apps/hacking/hacked-players-manager.hpp"
#include "utils/simple-timer.hpp"

class IdleState : public ConnectState {
public:
    explicit IdleState(RemotePlayerManager* remotePlayerManager,
                       HackedPlayersManager* hackedPlayersManager,
                       FDNConnectWirelessManager* fdnConnectWirelessManager,
                       RemoteDeviceCoordinator* remoteDeviceCoordinator);
    ~IdleState();

    void onStateMounted(Device* PDN) override;
    void onStateLoop(Device* PDN) override;
    void onStateDismounted(Device* PDN) override;

    bool isJackRequired(SerialIdentifier jack) override;

    void setConnectionHandler(std::function<void(const std::string&, const uint8_t*)> handler);

    bool transitionToPlayerDetected();
    bool transitionToConnectionDetected();
    bool transitionToUploadPending();

private:
    RemotePlayerManager* remotePlayerManager;
    HackedPlayersManager* hackedPlayersManager;
    FDNConnectWirelessManager* fdnConnectWirelessManager;

    std::function<void(const std::string&, const uint8_t*)> connectionHandler;

    SimpleTimer uploadTimer;
    bool connectionResolved = false;
    bool wasConnected       = false;
    int initialPlayerCount  = 0;

    static constexpr int UPLOAD_CHECK_INTERVAL_MS = 5 * 60 * 1000;
};
