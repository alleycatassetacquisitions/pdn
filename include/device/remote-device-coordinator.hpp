#pragma once

#include "device/serial-manager.hpp"
#include "wireless/remote-player-manager.hpp"
#include "apps/handshake/handshake.hpp"

enum class PortStatus {
    DISCONNECTED = 0,
    CONNECTED = 1,
    DAISY_CHAINED = 2, //Not implementing this yet.
};

struct PortState {
    SerialByState port;
    PortStatus status;
    RemotePlayer* peerList;
};

class RemoteDeviceCoordinator {
public:
    RemoteDeviceCoordinator();

    void initialize(RemotePlayerManager* remotePlayerManager, SerialManager* serialManager);

    void sync();

    PortStatus getPortStatus(SerialByState port);

    PortState getPortState(SerialByState port);

private:

    void emitHeartbeat();
    bool checkForHeartbeat();
    void notifyDisconnect();
    void notifyConnect();
    void notifyDaisyChained();

    PortStatus mapHandshakeStateToStatus(SerialByState port);

    void addDaisyChainedPeer(RemotePlayer remotePlayer);
    void removeDaisyChainedPeer(RemotePlayer remotePlayer);

    void registerPeer(RemotePlayer remotePlayer);
    void unregisterPeer(RemotePlayer remotePlayer);

    RemotePlayerManager* remotePlayerManager;
    SerialManager* serialManager;

    HandshakeApp primaryPortHandshake;
    HandshakeApp auxPortHandshake;

    SimpleTimer heartbeatTimer;
    SimpleTimer timeout;

    const int HEARTBEAT_INTERVAL = 250;
    const int TIMEOUT_INTERVAL = 700;

    std::function<void()> onPrimaryPortDisconnected;
};