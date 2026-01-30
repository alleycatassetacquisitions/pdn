#include "game/quickdraw-states.hpp"
#include "game/quickdraw.hpp"
#include "wireless/quickdraw-wireless-manager.hpp"
#include "game/match-manager.hpp"
#include "device/drivers/logger.hpp"

#define DUEL_TAG "DUEL_STATE"
//
// Created by Elli Furedy on 9/30/2024.
//
/*
if (peekGameComms() == ZAP) {
    readGameComms();
    writeGameComms(YOU_DEFEATED_ME);
    captured = true;
    return;
  } else if (peekGameComms() == YOU_DEFEATED_ME) {
    readGameComms();
    wonBattle = true;
    return;
  } else {
    readGameComms();
  }

  // primary.tick();

  if (startDuelTimer) {
    setTimer(DUEL_TIMEOUT);
    startDuelTimer = false;
  }

  if (timerExpired()) {
    // FastLED.setBrightness(0);
    bvbDuel = false;
    duelTimedOut = true;
  }
 */
Duel::Duel(Player* player, MatchManager* matchManager, QuickdrawWirelessManager* quickdrawWirelessManager) : State(DUEL) {
    this->player = player;
    this->matchManager = matchManager;
    this->quickdrawWirelessManager = quickdrawWirelessManager;
    LOG_I(DUEL_TAG, "Duel state created for player %s (Hunter: %d)", 
             player->getUserID().c_str(), player->isHunter());
}

Duel::~Duel() {
    LOG_I(DUEL_TAG, "Duel state destroyed");
    this->player = nullptr;
    this->matchManager = nullptr;
    this->quickdrawWirelessManager = nullptr;
}

void Duel::onStateMounted(Device *PDN) {
    LOG_I(DUEL_TAG, "Duel state mounted");
    matchManager->setDuelLocalStartTime(SimpleTimer::getPlatformClock()->milliseconds());

    LOG_I(DUEL_TAG, "Setting up button handlers");
    
    quickdrawWirelessManager->setPacketReceivedCallback(
        std::bind(&MatchManager::listenForMatchResults, matchManager, std::placeholders::_1)
    );

    PDN->getPrimaryButton()->setButtonPress(
        matchManager->getDuelButtonPush(), matchManager, ButtonInteraction::CLICK);

    PDN->getSecondaryButton()->setButtonPress(
        matchManager->getDuelButtonPush(), matchManager, ButtonInteraction::CLICK);

    duelTimer.setTimer(DUEL_TIMEOUT);

    LOG_I(DUEL_TAG, "Duel timer started for %d ms, duelStartTime: %lu", 
             DUEL_TIMEOUT, matchManager->getDuelLocalStartTime());
             
    PDN->getDisplay()->invalidateScreen()->
    drawImage(Quickdraw::getImageForAllegiance(player->getAllegiance(), ImageType::IDLE))->
    drawImage(Quickdraw::getImageForAllegiance(player->getAllegiance(), ImageType::DRAW))->
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
    
    if(duelTimer.expired()) {
        transitionToIdleState = true;
    }
}

bool Duel::transitionToIdle() {
    if (transitionToIdleState) {
        LOG_I(DUEL_TAG, "Transitioning to idle state");
    }
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
    LOG_I(DUEL_TAG, "Duel state dismounted - Cleanup");
    
    duelTimer.invalidate();
    LOG_I(DUEL_TAG, "Duel timer invalidated");

    transitionToDuelReceivedResultState = false;
    transitionToIdleState = false;
    transitionToDuelPushedState = false;
}
