#pragma once

#include "device/drivers/peer-comms-interface.hpp"
#include "device/drivers/serial-wrapper.hpp"

class RemoteDeviceCoordinator {
public:
    RemoteDeviceCoordinator();

    void initialize(PeerCommsInterface* peerComms, HWSerialWrapper* serial1, HWSerialWrapper* serial2);

    void sync();

private:

    void emitHeartbeat();
    bool checkForHeartbeat();
    void notifyDisconnect();
    void notifyConnect();

    void manageConnectedPeers();

    PeerCommsInterface* peerComms;
    HWSerialWrapper* serial1;
    HWSerialWrapper* serial2;
}