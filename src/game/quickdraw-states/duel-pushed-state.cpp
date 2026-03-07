#include "game/quickdraw-states.hpp"
#include "game/match-manager.hpp"
#include "device/device.hpp"
#include <string>

#define DUEL_PUSHED_TAG "DUEL_PUSHED"

DuelPushed::DuelPushed(Player* player, MatchManager* matchManager, RemoteDeviceCoordinator* remoteDeviceCoordinator, ChainContext* chainContext) : ConnectState(remoteDeviceCoordinator, DUEL_PUSHED) {
    this->player = player;
    this->matchManager = matchManager;
    this->chainContext_ = chainContext;
}

DuelPushed::~DuelPushed() {
    LOG_I(DUEL_PUSHED_TAG, "DuelPushed state destroyed");
    this->player = nullptr;
    this->matchManager = nullptr;
}

void DuelPushed::onStateMounted(Device *PDN) {
    LOG_I(DUEL_PUSHED_TAG, "DuelPushed state mounted");

    PDN->getPrimaryButton()->removeButtonCallbacks();
    PDN->getSecondaryButton()->removeButtonCallbacks();

    gracePeriodTimer.setTimer(DUEL_RESULT_GRACE_PERIOD);

    if (chainContext_) {
        remoteDeviceCoordinator->registerSerialHandler("confirm:", SerialIdentifier::INPUT_JACK,
            [this](const std::string& msg) {
                int count = std::stoi(msg.substr(8));
                chainContext_->confirmedSupporters = count;
            });
    }

    PDN->getHaptics()->setIntensity(0);
}

void DuelPushed::onStateLoop(Device *PDN) {
    gracePeriodTimer.updateTime();
}

void DuelPushed::onStateDismounted(Device *PDN) {
    LOG_I(DUEL_PUSHED_TAG, "DuelPushed state dismounted");

    if (!isConnected()) {
        matchManager->clearCurrentMatch();
    }

    if (chainContext_) {
        remoteDeviceCoordinator->unregisterSerialHandler("confirm:", SerialIdentifier::INPUT_JACK);
    }

    gracePeriodTimer.invalidate();
}

bool DuelPushed::isPrimaryRequired() {
    return player->isHunter();
}

bool DuelPushed::isAuxRequired() {
    return !player->isHunter();
}

bool DuelPushed::disconnectedBackToIdle() {
    return !isConnected();
}

bool DuelPushed::transitionToDuelResult() {
    return matchManager->matchResultsAreIn() || gracePeriodTimer.expired();
}