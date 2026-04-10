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

enum class PlayerDetectedStrength {
    WEAK,
    MEDIUM,
    STRONG
};

struct PlayerDetectionConfig {
    PlayerDetectedStrength strength;
    int timeout;
    uint8_t intensity;
};

class PlayerDetectedState : public ConnectState {
public:
    explicit PlayerDetectedState(RemotePlayerManager* remotePlayerManager,
                                 HackedPlayersManager* hackedPlayersManager,
                                 FDNConnectWirelessManager* fdnConnectWirelessManager,
                                 RemoteDeviceCoordinator* remoteDeviceCoordinator);
    ~PlayerDetectedState();

    void onStateMounted(Device* PDN) override;
    void onStateLoop(Device* PDN) override;
    void onStateDismounted(Device* PDN) override;

    bool isJackRequired(SerialIdentifier jack) override;

    void setConnectionHandler(std::function<void(const std::string&, const uint8_t*)> handler);

    bool transitionToIdle();
    bool transitionToConnectionDetected();

private:
    RemotePlayerManager* remotePlayerManager;
    HackedPlayersManager* hackedPlayersManager;
    FDNConnectWirelessManager* fdnConnectWirelessManager;

    std::function<void(const std::string&, const uint8_t*)> connectionHandler;

    SimpleTimer playerDetectedTimer;
    SimpleTimer glyphTimer;
    bool contentReady       = false;
    bool connectionResolved = false;
    bool wasConnected       = false;
    const PlayerDetectionConfig* activeConfig = nullptr;

    const PlayerDetectionConfig WEAK   = { PlayerDetectedStrength::WEAK,   2500,  80 };
    const PlayerDetectionConfig MEDIUM = { PlayerDetectedStrength::MEDIUM, 2500, 150 };
    const PlayerDetectionConfig STRONG = { PlayerDetectedStrength::STRONG, 5000, 255 };
};
