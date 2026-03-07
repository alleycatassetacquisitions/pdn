#include "device/drivers/serial-wrapper.hpp"
#include "game/quickdraw-states.hpp"
#include "game/quickdraw-resources.hpp"
#include "device/device.hpp"
#include "device/serial-manager.hpp"
#include <string>

DuelCountdown::DuelCountdown(Player* player, MatchManager* matchManager, RemoteDeviceCoordinator* remoteDeviceCoordinator, ChainContext* chainContext)
    : ConnectState(remoteDeviceCoordinator, DUEL_COUNTDOWN), chainContext_(chainContext) {
    this->player = player;
    this->matchManager = matchManager;
}

DuelCountdown::~DuelCountdown() {
    player = nullptr;
    matchManager = nullptr;
}


void DuelCountdown::onStateMounted(Device *PDN) {
    serialManager_ = PDN->getSerialManager();

    PDN->getDisplay()->
    invalidateScreen()->
    drawImage(getImageForAllegiance(player->getAllegiance(), getImageIdForStep(countdownQueue[currentStepIndex].step)))->
    render();

    PDN->getLightManager()->startAnimation(countdownQueue[currentStepIndex].animationConfig);

    PDN->getPrimaryButton()->setButtonPress(
        matchManager->getButtonMasher(),
        matchManager, ButtonInteraction::CLICK);

    PDN->getSecondaryButton()->setButtonPress(
        matchManager->getButtonMasher(),
        matchManager);

    PDN->getHaptics()->setIntensity(HAPTIC_INTENSITY);
    hapticTimer.setTimer(HAPTIC_DURATION);

    if (player->isHunter()) {
        const uint8_t* peerMac = remoteDeviceCoordinator->getPeerMac(SerialIdentifier::OUTPUT_JACK);
        if (peerMac != nullptr) {
            matchManager->initializeMatch(const_cast<uint8_t*>(peerMac));
        }
    }

    bool isSolo = (chainContext_ == nullptr || chainContext_->chainLength <= 1);

    if (!isSolo) {
        remoteDeviceCoordinator->registerSerialHandler("confirm:", SerialIdentifier::INPUT_JACK,
            [this](const std::string& msg) {
                int count = std::stoi(msg.substr(8));
                chainContext_->confirmedSupporters = count;
            });

        remoteDeviceCoordinator->setOnDisconnectCallback([this](SerialIdentifier disconnectedPort) {
            if (!serialManager_) return;
            SerialIdentifier notifyJack = (disconnectedPort == SerialIdentifier::OUTPUT_JACK)
                ? SerialIdentifier::INPUT_JACK : SerialIdentifier::OUTPUT_JACK;
            serialManager_->writeString("event:break", notifyJack);
        });
    } else {
        countdownTimer.setTimer(countdownQueue[currentStepIndex].countdownTimer);
        currentStepIndex++;
        countdownStarted_ = true;
    }
}


void DuelCountdown::onStateLoop(Device *PDN) {
    if (!countdownStarted_ && chainContext_ != nullptr &&
        chainContext_->confirmedSupporters >= chainContext_->chainLength - 1) {
        countdownTimer.setTimer(countdownQueue[currentStepIndex].countdownTimer);
        currentStepIndex++;
        countdownStarted_ = true;
        PDN->getSerialManager()->writeString("event:countdown", SerialIdentifier::INPUT_JACK);
    }

    if (!countdownStarted_) return;

    countdownTimer.updateTime();
    hapticTimer.updateTime();

    if (hapticTimer.expired()) {
        PDN->getHaptics()->setIntensity(0);
    }

    if (countdownTimer.expired()) {
        PDN->getHaptics()->setIntensity(HAPTIC_INTENSITY);
        hapticTimer.setTimer(HAPTIC_DURATION);
        if(countdownQueue[currentStepIndex].step == CountdownStep::BATTLE) {
            doBattle = true;
        } else {
            PDN->getDisplay()->
            invalidateScreen()->
            drawImage(getImageForAllegiance(player->getAllegiance(), getImageIdForStep(countdownQueue[currentStepIndex].step)))->
            render();

            PDN->getLightManager()->startAnimation(countdownQueue[currentStepIndex].animationConfig);

            countdownTimer.setTimer(countdownQueue[currentStepIndex].countdownTimer);
            currentStepIndex++;
        }
    }
}

ImageType DuelCountdown::getImageIdForStep(CountdownStep step) {
    switch(step) {
        case CountdownStep::THREE:
            return ImageType::COUNTDOWN_THREE;
        case CountdownStep::TWO:
            return ImageType::COUNTDOWN_TWO;
        default:
            return ImageType::COUNTDOWN_ONE;
    }
}


void DuelCountdown::onStateDismounted(Device *PDN) {
    if (!doBattle) {
        matchManager->clearCurrentMatch();
    }

    remoteDeviceCoordinator->unregisterSerialHandler("confirm:", SerialIdentifier::INPUT_JACK);
    remoteDeviceCoordinator->clearOnDisconnectCallback();
    serialManager_ = nullptr;

    doBattle = false;
    currentStepIndex = 0;
    countdownStarted_ = false;
    countdownTimer.invalidate();
    PDN->getPrimaryButton()->removeButtonCallbacks();
    PDN->getSecondaryButton()->removeButtonCallbacks();
}

bool DuelCountdown::shallWeBattle() {
    return doBattle;
}

bool DuelCountdown::disconnectedBackToIdle() {
    return !isConnected();
}

bool DuelCountdown::isPrimaryRequired() {
    return player->isHunter();
}

bool DuelCountdown::isAuxRequired() {
    return !player->isHunter();
}

bool DuelCountdown::countdownStarted() {
    return countdownStarted_;
}
