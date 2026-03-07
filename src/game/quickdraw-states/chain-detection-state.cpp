#include "game/quickdraw-states.hpp"
#include "device/device.hpp"
#include "device/serial-manager.hpp"
#include "device/remote-device-coordinator.hpp"
#include <string>
#include <stdexcept>

ChainDetectionState::ChainDetectionState(ChainContext* context, RemoteDeviceCoordinator* rdc)
    : State(CHAIN_DETECTION), context_(context), rdc_(rdc) {}

void ChainDetectionState::onStateMounted(Device* PDN) {
    *context_ = ChainContext{};
    SerialManager* serialManager = PDN->getSerialManager();

    // Always register "chain:" handler on OUTPUT_JACK to handle supporter/tail cases
    rdc_->registerSerialHandler("chain:", SerialIdentifier::OUTPUT_JACK,
        [this, PDN, serialManager](const std::string& msg) {
            // Parse N from "chain:N"
            int n = std::stoi(msg.substr(6));
            context_->position = n + 1;
            context_->role = ChainRole::SUPPORTER;

            if (rdc_->getPortStatus(SerialIdentifier::INPUT_JACK) == PortStatus::CONNECTED) {
                // Mid-chain supporter: forward chain message downstream
                serialManager->writeString("chain:" + std::to_string(n + 1), SerialIdentifier::INPUT_JACK);
                // Register len: handler on INPUT_JACK to receive the length reply
                rdc_->registerSerialHandler("len:", SerialIdentifier::INPUT_JACK,
                    [this, PDN, serialManager](const std::string& lenMsg) {
                        int m = std::stoi(lenMsg.substr(4));
                        context_->chainLength = m;
                        // Relay len: upstream
                        serialManager->writeString("len:" + std::to_string(m), SerialIdentifier::OUTPUT_JACK);
                        context_->detectionComplete = true;
                    });
            } else {
                // Tail supporter: send len back upstream
                context_->chainLength = n + 1;
                serialManager->writeString("len:" + std::to_string(n + 1), SerialIdentifier::OUTPUT_JACK);
                context_->detectionComplete = true;
            }
        });

    if (rdc_->getPortStatus(SerialIdentifier::INPUT_JACK) == PortStatus::CONNECTED) {
        // Potential champion: send chain:0 downstream and await len: reply
        serialManager->writeString("chain:0", SerialIdentifier::INPUT_JACK);
        rdc_->registerSerialHandler("len:", SerialIdentifier::INPUT_JACK,
            [this](const std::string& msg) {
                if (context_->role == ChainRole::SUPPORTER) {
                    // Already handled in chain: handler's len: registration
                    return;
                }
                int m = std::stoi(msg.substr(4));
                context_->chainLength = m;
                context_->detectionComplete = true;
            });
    } else {
        // Input disconnected: solo player (or potential tail if chain: arrives on output)
        context_->chainLength = 1;
        context_->detectionComplete = true;
    }
}

void ChainDetectionState::onStateLoop(Device* PDN) {}

void ChainDetectionState::onStateDismounted(Device* PDN) {
    rdc_->unregisterSerialHandler("chain:", SerialIdentifier::OUTPUT_JACK);
    rdc_->unregisterSerialHandler("len:", SerialIdentifier::INPUT_JACK);
}

bool ChainDetectionState::detectionComplete() {
    return context_->detectionComplete;
}

bool ChainDetectionState::isSupporterRole() {
    return context_->detectionComplete && context_->role == ChainRole::SUPPORTER;
}

bool ChainDetectionState::isChampionRole() {
    return context_->detectionComplete && context_->role == ChainRole::CHAMPION;
}
