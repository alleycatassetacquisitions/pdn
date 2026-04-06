#include "apps/handshake/handshake-states.hpp"
#include "device/device.hpp"

#define TAG "OUTPUT_SEND_ID_STATE"

OutputSendIdState::OutputSendIdState(HandshakeWirelessManager* handshakeWirelessManager) : State(HandshakeStateId::OUTPUT_SEND_ID_STATE) {
    this->handshakeWirelessManager = handshakeWirelessManager;
}

OutputSendIdState::~OutputSendIdState() {
    handshakeWirelessManager = nullptr;
}


void OutputSendIdState::onStateMounted(Device *PDN) {
    handshakeWirelessManager->setPacketReceivedCallback(std::bind(&OutputSendIdState::onHandshakeCommandReceived, this, std::placeholders::_1), SerialIdentifier::OUTPUT_JACK);

    handshakeWirelessManager->sendPacket(HSCommand::EXCHANGE_ID, SerialIdentifier::OUTPUT_JACK);
    retryTimer.setTimer(RETRY_INTERVAL_MS);
}

void OutputSendIdState::onHandshakeCommandReceived(HandshakeCommand command) {
    if (command.command == HSCommand::EXCHANGE_ID) {
        handshakeWirelessManager->sendPacket(HSCommand::EXCHANGE_ID, SerialIdentifier::OUTPUT_JACK);
        transitionToConnectionSuccessfulState = true;
    }
}

void OutputSendIdState::onStateLoop(Device *PDN) {
    retryTimer.updateTime();
    if (retryTimer.expired()) {
        handshakeWirelessManager->sendPacket(HSCommand::EXCHANGE_ID, SerialIdentifier::OUTPUT_JACK);
        retryTimer.setTimer(RETRY_INTERVAL_MS);
    }
}

void OutputSendIdState::onStateDismounted(Device *PDN) {
    LOG_I(TAG, "State dismounted");
    transitionToConnectionSuccessfulState = false;
    retryTimer.invalidate();
    handshakeWirelessManager->clearCallback(SerialIdentifier::OUTPUT_JACK);
}

bool OutputSendIdState::transitionToConnected() {
    return transitionToConnectionSuccessfulState;
}
