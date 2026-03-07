#include "game/quickdraw-states.hpp"
#include "game/quickdraw-resources.hpp"
#include "game/match-manager.hpp"
#include "device/drivers/logger.hpp"
#include "device/device.hpp"
#include "device/serial-manager.hpp"

#define DUEL_TAG "DUEL_STATE"

Duel::Duel(Player* player, MatchManager* matchManager, RemoteDeviceCoordinator* remoteDeviceCoordinator, ChainContext* chainContext) : ConnectState(remoteDeviceCoordinator, DUEL) {
    this->player = player;
    this->matchManager = matchManager;
    this->chainContext_ = chainContext;
}

Duel::~Duel() {
    LOG_I(DUEL_TAG, "Duel state destroyed");
    this->player = nullptr;
    this->matchManager = nullptr;
}

void Duel::onStateMounted(Device *PDN) {
    LOG_I(DUEL_TAG, "Duel state mounted");
    serialManager_ = PDN->getSerialManager();
    matchManager->setDuelLocalStartTime(SimpleTimer::getPlatformClock()->milliseconds());

    LOG_I(DUEL_TAG, "Setting up button handlers");

    auto duelButtonPush = matchManager->getDuelButtonPush();
    PDN->getPrimaryButton()->setButtonPress(duelButtonPush, matchManager, ButtonInteraction::CLICK);
    PDN->getSecondaryButton()->setButtonPress(duelButtonPush, matchManager, ButtonInteraction::CLICK);

    if (chainContext_ && chainContext_->chainLength > 1) {
        PDN->getSerialManager()->writeString("event:draw", SerialIdentifier::INPUT_JACK);
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
    }

    duelTimer.setTimer(DUEL_TIMEOUT);

    LOG_I(DUEL_TAG, "Duel timer started for %d ms, duelStartTime: %lu", 
             DUEL_TIMEOUT, matchManager->getDuelLocalStartTime());
             
    PDN->getDisplay()->invalidateScreen()->
    drawImage(getImageForAllegiance(player->getAllegiance(), ImageType::IDLE))->
    drawImage(getImageForAllegiance(player->getAllegiance(), ImageType::DRAW))->
    render();
    
    LOG_I(DUEL_TAG, "Draw image displayed for allegiance: %d", player->getAllegiance());

    AnimationConfig config;
    config.type = AnimationType::COUNTDOWN;
    config.speed = 16;
    config.loopDelayMs = 0;
    config.loop = false;
    config.initialState = COUNTDOWN_DUEL_STATE;
    
    PDN->getLightManager()->startAnimation(config);

    PDN->getHaptics()->setIntensity(175);
}

void Duel::onStateLoop(Device *PDN) {
    duelTimer.updateTime();

    if(matchManager->getHasReceivedDrawResult()) {
        transitionToDuelReceivedResultState = true;
        return;
    } else if(matchManager->getHasPressedButton()) {
        transitionToDuelPushedState = true;
        return;
    }
    
    if(duelTimer.expired() || !isConnected()) {
        transitionToIdleState = true;
    }
}

bool Duel::transitionToIdle() {
    return transitionToIdleState;
}

bool Duel::transitionToDuelPushed() {
    if (transitionToDuelPushedState) {
        LOG_I(DUEL_TAG, "Transitioning to duel pushed state");
    }
    return transitionToDuelPushedState;
}

bool Duel::transitionToDuelReceivedResult() {
    if (transitionToDuelReceivedResultState) {
        LOG_I(DUEL_TAG, "Transitioning to duel result state");
    }
    return transitionToDuelReceivedResultState;
}

void Duel::onStateDismounted(Device *PDN) {
    if(transitionToIdleState) {
        PDN->getHaptics()->off();
        matchManager->clearCurrentMatch();
        PDN->getPrimaryButton()->removeButtonCallbacks();
        PDN->getSecondaryButton()->removeButtonCallbacks();
    } else if(transitionToDuelReceivedResultState) {
        PDN->getPrimaryButton()->removeButtonCallbacks();
        PDN->getSecondaryButton()->removeButtonCallbacks();
    }

    if (chainContext_) {
        remoteDeviceCoordinator->unregisterSerialHandler("confirm:", SerialIdentifier::INPUT_JACK);
        remoteDeviceCoordinator->clearOnDisconnectCallback();
    }
    serialManager_ = nullptr;

    LOG_I(DUEL_TAG, "Duel state dismounted - Cleanup");
    
    duelTimer.invalidate();
    LOG_I(DUEL_TAG, "Duel timer invalidated");

    transitionToDuelReceivedResultState = false;
    transitionToIdleState = false;
    transitionToDuelPushedState = false;
}

bool Duel::isPrimaryRequired() {
    return player->isHunter();
}

bool Duel::isAuxRequired() {
    return !player->isHunter();
}