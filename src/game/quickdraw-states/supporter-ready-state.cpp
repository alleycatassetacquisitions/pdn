#include "game/quickdraw-states.hpp"
#include "device/device.hpp"
#include "device/serial-manager.hpp"
#include "device/remote-device-coordinator.hpp"
#include <string>

SupporterReadyState::SupporterReadyState(ChainContext* context, RemoteDeviceCoordinator* rdc)
    : State(SUPPORTER_READY), context_(context), rdc_(rdc) {}

void SupporterReadyState::onStateMounted(Device* PDN) {
    serialManager_ = PDN->getSerialManager();

    rdc_->registerSerialHandler("event:", SerialIdentifier::OUTPUT_JACK,
        [this](const std::string& msg) {
            std::string eventType = msg.substr(6);

            serialManager_->writeString(msg, SerialIdentifier::INPUT_JACK);

            if (eventType == "countdown") {
                receivedCountdown_ = true;
            } else if (eventType == "win") {
                transitionToWin_ = true;
            } else if (eventType == "loss") {
                transitionToLose_ = true;
            } else if (eventType == "break") {
                transitionToIdle_ = true;
            }
        });

    rdc_->registerSerialHandler("confirm:", SerialIdentifier::INPUT_JACK,
        [this](const std::string& msg) {
            int count = std::stoi(msg.substr(8));
            if (hasPressed_) {
                count += 1;
            }
            serialManager_->writeString("confirm:" + std::to_string(count), SerialIdentifier::OUTPUT_JACK);
        });

    parameterizedCallbackFunction pressHandler = [](void* ctx) {
        SupporterReadyState* self = static_cast<SupporterReadyState*>(ctx);
        if (self->hasPressed_) return;
        self->hasPressed_ = true;
        self->sendInitialConfirm();
    };

    PDN->getPrimaryButton()->setButtonPress(pressHandler, this, ButtonInteraction::CLICK);
    PDN->getSecondaryButton()->setButtonPress(pressHandler, this, ButtonInteraction::CLICK);
}

void SupporterReadyState::onStateLoop(Device* PDN) {}

void SupporterReadyState::onStateDismounted(Device* PDN) {
    rdc_->unregisterSerialHandler("event:", SerialIdentifier::OUTPUT_JACK);
    rdc_->unregisterSerialHandler("confirm:", SerialIdentifier::INPUT_JACK);
    PDN->getPrimaryButton()->removeButtonCallbacks();
    PDN->getSecondaryButton()->removeButtonCallbacks();
    hasPressed_ = false;
    transitionToWin_ = false;
    transitionToLose_ = false;
    transitionToIdle_ = false;
    receivedCountdown_ = false;
    serialManager_ = nullptr;
}

void SupporterReadyState::sendInitialConfirm() {
    if (serialManager_) {
        serialManager_->writeString("confirm:1", SerialIdentifier::OUTPUT_JACK);
    }
}


bool SupporterReadyState::transitionToWin() { return transitionToWin_; }
bool SupporterReadyState::transitionToLose() { return transitionToLose_; }
bool SupporterReadyState::transitionToIdle() { return transitionToIdle_; }
bool SupporterReadyState::receivedCountdown() { return receivedCountdown_; }
