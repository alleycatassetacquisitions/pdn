#pragma once

#include "state/state.hpp"
#include "device/remote-device-coordinator.hpp"
#include "utils/simple-timer.hpp"

class ConnectState : public State {
public:
    ConnectState(RemoteDeviceCoordinator* remoteDeviceCoordinator, int stateId) : State(stateId) {
        this->remoteDeviceCoordinator = remoteDeviceCoordinator;
    };

    virtual ~ConnectState() {
        remoteDeviceCoordinator = nullptr;
    }

    const DeviceType getPeerDeviceType(SerialIdentifier port) const {
        return remoteDeviceCoordinator->getPeerDeviceType(port);
    }

    bool isConnected() {
        auto connectedOrChain = [this](SerialIdentifier jack) {
            PortStatus s = remoteDeviceCoordinator->getPortStatus(jack);
            return s == PortStatus::CONNECTED || s == PortStatus::DAISY_CHAINED;
        };
        return (isPrimaryRequired() && connectedOrChain(SerialIdentifier::OUTPUT_JACK)) ||
               (isAuxRequired() && connectedOrChain(SerialIdentifier::INPUT_JACK));
    }

    // Returns true only when isConnected() has been false continuously for
    // kDisconnectDebounceMs. Cable nudges and chain re-evaluations flicker the
    // raw signal for one or two ticks; treating every false reading as a real
    // disconnect aborts duel countdowns mid-shootout and orphans the duelist.
    // Self-managing: each call advances the internal timer based on the live
    // isConnected() result.
    bool isPersistentlyDisconnected() {
        if (isConnected()) {
            disconnectedDebounceTimer_.invalidate();
            return false;
        }
        if (!disconnectedDebounceTimer_.isRunning()) {
            disconnectedDebounceTimer_.setTimer(kDisconnectDebounceMs);
            return false;
        }
        return disconnectedDebounceTimer_.expired();
    }

protected:
    RemoteDeviceCoordinator* remoteDeviceCoordinator;

    virtual bool isPrimaryRequired() = 0;
    virtual bool isAuxRequired() = 0;

private:
    static constexpr unsigned long kDisconnectDebounceMs = 500;
    SimpleTimer disconnectedDebounceTimer_;
};