#pragma once

#include "state/connect-state.hpp"
#include "device/fdn.hpp"

// Base class for FDN states that check for player connections.
// FDN has two input jacks (INPUT_JACK and INPUT_JACK_SECONDARY) and no output jack.
// isConnected() returns true when either input jack has a connected or daisy-chained peer.
class FDNConnectState : public ConnectState<FDN> {
public:
    FDNConnectState(RemoteDeviceCoordinator* remoteDeviceCoordinator, int stateId)
        : ConnectState<FDN>(remoteDeviceCoordinator, stateId) {}

    // FDN has no output jack.
    bool isPrimaryRequired() override { return false; }

    // FDN input jacks — both considered for connectivity.
    bool isAuxRequired() override { return true; }
    bool isSecondaryRequired() override { return true; }
};
