#include "game/quickdraw-states.hpp"
#include "game/quickdraw-resources.hpp"
#include "device/animation/countdown-animation.hpp"
#include "game/match-manager.hpp"
#include "game/chain-duel-manager.hpp"
#include "game/shootout-manager.hpp"
#include "device/drivers/logger.hpp"
#include "device/device.hpp"

#define DUEL_TAG "DUEL_STATE"

Duel::Duel(Player* player, MatchManager* matchManager, RemoteDeviceCoordinator* remoteDeviceCoordinator, ChainDuelManager* chainDuelManager, ShootoutManager* shootoutManager) : ConnectState<PDN>(remoteDeviceCoordinator, DUEL) {
    this->player = player;
    this->matchManager = matchManager;
    this->chainDuelManager = chainDuelManager;
    this->shootoutManager = shootoutManager;
}

Duel::~Duel() {
    LOG_I(DUEL_TAG, "Duel state destroyed");
    this->player = nullptr;
    this->matchManager = nullptr;
}

void Duel::onStateMounted(PDN* pdn) {
    LOG_I(DUEL_TAG, "Duel state mounted");

    // Arm the supporter chain for confirmations during the draw window.
    chainDuelManager->sendGameEventToSupporters(ChainGameEventType::DRAW);

    matchManager->setDuelLocalStartTime(SimpleTimer::getPlatformClock()->milliseconds());

    LOG_I(DUEL_TAG, "Setting up button handlers");

    auto duelButtonPush = matchManager->getDuelButtonPush();
    pdn->getPrimaryButton()->setButtonPress(duelButtonPush, matchManager, ButtonInteraction::CLICK);
    pdn->getSecondaryButton()->setButtonPress(duelButtonPush, matchManager, ButtonInteraction::CLICK);

    duelTimer.setTimer(DUEL_TIMEOUT);

    LOG_I(DUEL_TAG, "Duel timer started for %d ms, duelStartTime: %lu", 
             DUEL_TIMEOUT, matchManager->getDuelLocalStartTime());
             
    pdn->getDisplay()->invalidateScreen()->
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
    
    pdn->getLightManager()->startAnimation(new CountdownAnimation(), config);

    pdn->getHaptics()->setIntensity(175);
}

void Duel::onStateLoop(PDN* pdn) {
    duelTimer.updateTime();

    if(matchManager->getHasReceivedDrawResult()) {
        transitionToDuelReceivedResultState = true;
        return;
    } else if(matchManager->getHasPressedButton()) {
        transitionToDuelPushedState = true;
        return;
    }
    
    if (!duelTimer.expired() && isConnected()) return;

    bool shootoutTimeout = duelTimer.expired() && shootoutManager && shootoutManager->active();
    if (!shootoutTimeout) {
        transitionToIdleState = true;
        return;
    }

    // Shootout timeout: hunter forfeits, bounty wins.
    if (player->isHunter()) {
        transitionToShootoutEliminatedState = true;
        return;
    }
    shootoutManager->reportLocalWin();
    transitionToShootoutSpectatorState = true;
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

bool Duel::transitionToShootoutSpectator() {
    return transitionToShootoutSpectatorState;
}

bool Duel::transitionToShootoutEliminated() {
    return transitionToShootoutEliminatedState;
}

void Duel::onStateDismounted(PDN* pdn) {
    if(transitionToIdleState || transitionToShootoutSpectatorState
       || transitionToShootoutEliminatedState) {
        pdn->getHaptics()->off();
        matchManager->clearCurrentMatch();
        pdn->getPrimaryButton()->removeButtonCallbacks();
        pdn->getSecondaryButton()->removeButtonCallbacks();
    } else if(transitionToDuelReceivedResultState) {
        pdn->getPrimaryButton()->removeButtonCallbacks();
        pdn->getSecondaryButton()->removeButtonCallbacks();
    }

    LOG_I(DUEL_TAG, "Duel state dismounted - Cleanup");

    duelTimer.invalidate();
    LOG_I(DUEL_TAG, "Duel timer invalidated");

    transitionToDuelReceivedResultState = false;
    transitionToIdleState = false;
    transitionToDuelPushedState = false;
    transitionToShootoutSpectatorState = false;
    transitionToShootoutEliminatedState = false;
}

bool Duel::isPrimaryRequired() {
    return player->isHunter();
}

bool Duel::isAuxRequired() {
    return !player->isHunter();
}