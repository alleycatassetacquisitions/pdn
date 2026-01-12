#include "device/pdn.hpp"
#include "game/quickdraw-states.hpp"
#include "game/quickdraw.hpp"
#include "wireless/quickdraw-wireless-manager.hpp"
#include "game/match-manager.hpp"
#include "logger.hpp"

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
Duel::Duel(Player* player, MatchManager* matchManager) : State(DUEL) {
    this->player = player;
    this->matchManager = matchManager;
    LOG_I(DUEL_TAG, "Duel state created for player %s (Hunter: %d)", 
             player->getUserID().c_str(), player->isHunter());
}

Duel::~Duel() {
    LOG_I(DUEL_TAG, "Duel state destroyed");
    this->player = nullptr;
    this->matchManager = nullptr;
}

void Duel::onStateMounted(Device *PDN) {
    LOG_I(DUEL_TAG, "Duel state mounted");
    matchManager->setDuelLocalStartTime(millis());

    LOG_I(DUEL_TAG, "Setting up button handlers");
    
    QuickdrawWirelessManager::GetInstance()->setPacketReceivedCallback(
        std::bind(&MatchManager::listenForMatchResults, matchManager, std::placeholders::_1)
    );

    PDN->setButtonClick(
        ButtonInteraction::CLICK,
        ButtonIdentifier::PRIMARY_BUTTON,
        matchManager->getDuelButtonPush(), matchManager);

    PDN->setButtonClick(
        ButtonInteraction::CLICK,
        ButtonIdentifier::SECONDARY_BUTTON,
        matchManager->getDuelButtonPush(), matchManager);

    duelTimer.setTimer(DUEL_TIMEOUT);

    LOG_I(DUEL_TAG, "Duel timer started for %d ms, duelStartTime: %lu", 
             DUEL_TIMEOUT, matchManager->getDuelLocalStartTime());
             
    PDN->invalidateScreen()->
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
    
    PDN->startAnimation(config);

    PDN->setVibration(175);
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
        // PDN->removeButtonCallbacks(ButtonIdentifier::PRIMARY_BUTTON);
        // PDN->removeButtonCallbacks(ButtonIdentifier::SECONDARY_BUTTON);
        
        // if(!hasReceivedDrawResult && !hasPressedButton) {
        //     LOG_W(DUEL_TAG, "Duel timer expired with no results and no button press - transitioning to idle");
        //     transitionToIdleState = true;
        // } else if(hasReceivedDrawResult && !hasPressedButton) {
        //     LOG_W(DUEL_TAG, "Duel timer expired with results but no button press - marking as timeout (-1)");
            
        //     player->isHunter() ?
        //     MatchManager::GetInstance()->setHunterDrawTime(DUEL_NO_RESULT_TIME):
        //     MatchManager::GetInstance()->setBountyDrawTime(DUEL_NO_RESULT_TIME);

        //     QuickdrawWirelessManager::GetInstance()->broadcastPacket(
        //         player->getOpponentMacAddress() ? *player->getOpponentMacAddress() : "",
        //         QDCommand::DRAW_RESULT,
        //         *MatchManager::GetInstance()->getCurrentMatch()
        //     );

        //     LOG_I(DUEL_TAG, "Transitioning to duel result state (timeout scenario)");
        //     transitionToDuelReceivedResultState = true;
        // } else if(!hasReceivedDrawResult && hasPressedButton) {
        //     LOG_W(DUEL_TAG, "Duel timer expired with button press but no results - marking as timeout (-1)");
        //     transitionToDuelReceivedResultState = true;
        // }
    // }
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
