#pragma once

#include "state/state.hpp"
#include "utils/simple-timer.hpp"
#include "wireless/handshake-wireless-manager.hpp"

enum HandshakeStateId {

    OUTPUT_IDLE_STATE = 80, // Output waiting to receive mac from foreign input port. When mac detected, send id to foreign mac and transition.
    OUTPUT_SEND_ID_STATE = 81, // Waiting for id from input, then sends final ack over esp-now to received MAC.
    OUTPUT_CONNECTED_STATE = 82, // Output Port receiving hb from foreign input port. If timeout is reached without hb, sends notify disconnect message to foreign mac.

    INPUT_IDLE_STATE = 83, // Input emitting mac over serial every 250 ms. Transitions when receiving mac over esp-now from foreign device.
    INPUT_SEND_ID_STATE = 84, // Input Port second state. Sends ACK and player id over esp-now to received MAC.
    INPUT_CONNECTED_STATE = 85 // sends hb over serial every 50 ms. if receive disconnect message, transition to INPUT_IDLE_STATE.
};
    
class OutputIdleState : public State {
public:
    explicit OutputIdleState(HandshakeWirelessManager* handshakeWirelessManager);
    ~OutputIdleState();

    void onStateMounted(Device *PDN) override;
    void onStateLoop(Device *PDN) override;
    void onStateDismounted(Device *PDN) override;
    bool transitionToOutputSendId();

    void onConnectionStarted(std::string remoteMac);

private:
    bool transitionToOutputSendIdState = false;
    HandshakeWirelessManager* handshakeWirelessManager;
};

class OutputSendIdState : public State {
public:
    OutputSendIdState(HandshakeWirelessManager* handshakeWirelessManager);
    ~OutputSendIdState();

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

class InputIdleState : public State {
public:
    InputIdleState(HandshakeWirelessManager* handshakeWirelessManager, SerialIdentifier jack);
    ~InputIdleState();

    void onStateMounted(Device *PDN) override;
    void onStateLoop(Device *PDN) override;
    void onStateDismounted(Device *PDN) override;
    bool transitionToSendId();

    void onHandshakeCommandReceived(HandshakeCommand command);

private:
    HandshakeWirelessManager* handshakeWirelessManager;
    SerialIdentifier jack;
    SimpleTimer emitMacTimer;
    const int emitMacInterval = 250;
    bool transitionToSendIdState = false;
};

class InputSendIdState : public State {
public:
    InputSendIdState(HandshakeWirelessManager* handshakeWirelessManager, SerialIdentifier jack);
    ~InputSendIdState();

    void onStateMounted(Device *PDN) override;
    void onStateLoop(Device *PDN) override;
    void onStateDismounted(Device *PDN) override;

    bool transitionToConnected();
    void onHandshakeCommandReceived(HandshakeCommand command);

private:
    HandshakeWirelessManager* handshakeWirelessManager;
    SerialIdentifier jack;
    bool transitionToConnectedState = false;
};
