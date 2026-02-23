#pragma once

#include "state/state.hpp"
#include "utils/simple-timer.hpp"
#include "wireless/handshake-wireless-manager.hpp"

enum HandshakeStateId {

    PRIMARY_IDLE_STATE = 80, // Primary waiting to receive mac from foreign aux port. When mac detected, send id to foreign mac and transition.
    PRIMARY_SEND_ID_STATE = 81, // Waiting for id from aux, then sends final ack over esp-now to received MAC.
    PRIMARY_CONNECTED_STATE = 82, // Primary Port receiving hb from foreign aux port. If timeout is reached without hb, sends notify disconnect message to foreign mac.

    AUXILIARY_IDLE_STATE = 83, // Auxiliary emitting mac over serial every 250 ms. Transitions when receiving mac over esp-now from foreign device.
    AUXILIARY_SEND_ID_STATE = 84, // Auxiliary Port second state. Sends ACK and player id over esp-now to received MAC.
    AUXILIARY_CONNECTED_STATE = 85 // sends hb over serial every 50 ms. if receive disconnect message, transition to AUXILIARY_IDLE_STATE.
};
    
class PrimaryIdleState : public State {
public:
    explicit PrimaryIdleState(HandshakeWirelessManager* handshakeWirelessManager);
    ~PrimaryIdleState();

    void onStateMounted(Device *PDN) override;
    void onStateLoop(Device *PDN) override;
    void onStateDismounted(Device *PDN) override;
    bool transitionToPrimarySendId();

    void onConnectionStarted(std::string remoteMac);

private:
    bool transitionToPrimarySendIDState = false;
    HandshakeWirelessManager* handshakeWirelessManager;
};

class PrimarySendIdState : public State {
public:
    PrimarySendIdState(HandshakeWirelessManager* handshakeWirelessManager);
    ~PrimarySendIdState();

    void onStateMounted(Device *PDN) override;
    void onStateLoop(Device *PDN) override;
    void onStateDismounted(Device *PDN) override;
    bool transitionToConnected();

    void onHandshakeCommandReceived(HandshakeCommand command);

private:
    HandshakeWirelessManager* handshakeWirelessManager;
    bool transitionToConnectionSuccessfulState = false;
};

class HandshakeConnectedState : public State {
public:
    HandshakeConnectedState(HandshakeWirelessManager* handshakeWirelessManager, SerialIdentifier jack, int stateId);
    ~HandshakeConnectedState();

    void onStateMounted(Device *PDN) override;
    void onStateLoop(Device *PDN) override;
    void onStateDismounted(Device *PDN) override;
    bool transitionToIdle();

    void heartbeatMonitorStringCallback(const std::string& message);
    void listenForNotifyDisconnectCommand(HandshakeCommand command);

private:
    HandshakeWirelessManager* handshakeWirelessManager;
    SerialIdentifier jack;

    SimpleTimer emitHeartbeatTimer;
    const int emitHeartbeatInterval = 50;

    SimpleTimer heartbeatMonitorTimer;
    const int firstHeartbeatTimeout = 400;
    const int heartbeatMonitorInterval = 150;
    bool transitionToIdleState = false;
};

class AuxiliaryIdleState : public State {
public:
    explicit AuxiliaryIdleState(HandshakeWirelessManager* handshakeWirelessManager);
    ~AuxiliaryIdleState();

    void onStateMounted(Device *PDN) override;
    void onStateLoop(Device *PDN) override;
    void onStateDismounted(Device *PDN) override;
    bool transitionToSendId();

    void onHandshakeCommandReceived(HandshakeCommand command);

private:
    HandshakeWirelessManager* handshakeWirelessManager;
    SimpleTimer emitMacTimer;
    const int emitMacInterval = 250;
    bool transitionToSendIdState = false;
};

class AuxiliarySendIdState : public State {
public:
    explicit AuxiliarySendIdState(HandshakeWirelessManager* handshakeWirelessManager);
    ~AuxiliarySendIdState();

    void onStateMounted(Device *PDN) override;
    void onStateLoop(Device *PDN) override;
    void onStateDismounted(Device *PDN) override;

    bool transitionToConnected();
    void onHandshakeCommandReceived(HandshakeCommand command);

private:
    HandshakeWirelessManager* handshakeWirelessManager;
    bool transitionToConnectedState = false;
};
