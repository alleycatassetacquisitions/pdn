#include "device/pdn.hpp"
#include "game/quickdraw-states.hpp"
#include "game/quickdraw.hpp"
#include "wireless/quickdraw-wireless-manager.hpp"
#include "game/match-manager.hpp"
#include <esp_log.h>

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
Duel::Duel(Player* player) : State(DUEL) {
    this->player = player;
    ESP_LOGI(DUEL_TAG, "Duel state created for player %s (Hunter: %d)", 
             player->getUserID().c_str(), player->isHunter());
}

Duel::~Duel() {
    ESP_LOGI(DUEL_TAG, "Duel state destroyed");
    this->player = nullptr;
}

void Duel::onStateMounted(Device *PDN) {
    ESP_LOGI(DUEL_TAG, "Duel state mounted");
    
    buttonPress = [](void *ctx) {
        if (!ctx) {
            ESP_LOGE(DUEL_TAG, "Button press handler received null context");
            return;
        }

        Duel* duelState = static_cast<Duel*>(ctx);
        if (!duelState->player) {
            ESP_LOGE(DUEL_TAG, "Button press handler - player is null");
            return;
        }

        if(duelState->hasPressedButton) {
            ESP_LOGI(DUEL_TAG, "Button already pressed - skipping");
            return;
        }

        Player *player = duelState->player;
        Device *PDN = PDN::GetInstance();
        if (!PDN) {
            ESP_LOGE(DUEL_TAG, "Button press handler - PDN is null");
            return;
        }

        unsigned long duelStartTime = duelState->duelStartTime;

        unsigned long reactionTimeMs = millis() - duelStartTime;

        ESP_LOGI(DUEL_TAG, "Button pressed! Reaction time: %lu ms for %s", 
                reactionTimeMs, player->isHunter() ? "Hunter" : "Bounty");

        player->isHunter() ? 
        MatchManager::GetInstance()->setHunterDrawTime(reactionTimeMs) 
        : MatchManager::GetInstance()->setBountyDrawTime(reactionTimeMs);

        ESP_LOGI(DUEL_TAG, "Stored reaction time in MatchManager");

        // Send a packet with the reaction time
        if (!player->getOpponentMacAddress()) {
            ESP_LOGE(DUEL_TAG, "Cannot send packet - opponent MAC address is null");
            return;
        }

        ESP_LOGI(DUEL_TAG, "Broadcasting DRAW_RESULT to opponent MAC: %s", 
                player->getOpponentMacAddress()->c_str());
                
        QuickdrawWirelessManager::GetInstance()->broadcastPacket(
            *player->getOpponentMacAddress(),
            QDCommand::DRAW_RESULT,
            *MatchManager::GetInstance()->getCurrentMatch()
        );

        duelState->hasPressedButton = true;

        if(duelState->hasReceivedDrawResult) {
            ESP_LOGI(DUEL_TAG, "Already have draw result when button is pressed");
            duelState->transitionToDuelResultState = true;
        }
        
        ESP_LOGI(DUEL_TAG, "Reaction time: %lu ms", reactionTimeMs);
    };

    ESP_LOGI(DUEL_TAG, "Setting up button handlers");

    PDN->setButtonClick(
        ButtonInteraction::CLICK,
        ButtonIdentifier::PRIMARY_BUTTON,
        buttonPress, this);

    PDN->setButtonClick(
        ButtonInteraction::CLICK,
        ButtonIdentifier::SECONDARY_BUTTON,
        buttonPress, this);

    duelTimer.setTimer(DUEL_TIMEOUT);
    duelStartTime = millis();
    ESP_LOGI(DUEL_TAG, "Duel timer started for %d ms, duelStartTime: %lu", 
             DUEL_TIMEOUT, duelStartTime);
             
    PDN->invalidateScreen()->
    drawImage(Quickdraw::getImageForAllegiance(player->getAllegiance(), ImageType::DRAW))->
    render();
    
    ESP_LOGI(DUEL_TAG, "Draw image displayed for allegiance: %d", player->getAllegiance());
}

void Duel::onStateLoop(Device *PDN) {
    duelTimer.updateTime();
    
    if(duelTimer.expired()) {
        
        PDN->removeButtonCallbacks(ButtonIdentifier::PRIMARY_BUTTON);
        PDN->removeButtonCallbacks(ButtonIdentifier::SECONDARY_BUTTON);
        
        if(!hasReceivedDrawResult && !hasPressedButton) {
            ESP_LOGW(DUEL_TAG, "Duel timer expired with no results and no button press - transitioning to idle");
            transitionToIdleState = true;
        } else if(hasReceivedDrawResult && !hasPressedButton) {
            ESP_LOGW(DUEL_TAG, "Duel timer expired with results but no button press - marking as timeout (-1)");
            
            player->isHunter() ?
            MatchManager::GetInstance()->setHunterDrawTime(DUEL_NO_RESULT_TIME):
            MatchManager::GetInstance()->setBountyDrawTime(DUEL_NO_RESULT_TIME);

            QuickdrawWirelessManager::GetInstance()->broadcastPacket(
                player->getOpponentMacAddress() ? *player->getOpponentMacAddress() : "",
                QDCommand::DRAW_RESULT,
                *MatchManager::GetInstance()->getCurrentMatch()
            );

            ESP_LOGI(DUEL_TAG, "Transitioning to duel result state (timeout scenario)");
            transitionToDuelResultState = true;
        } else if(!hasReceivedDrawResult && hasPressedButton) {
            ESP_LOGW(DUEL_TAG, "Duel timer expired with button press but no results - marking as timeout (-1)");
            transitionToDuelResultState = true;
        }
    }
}

void Duel::onQuickdrawCommandReceived(QuickdrawCommand command) {
    if (!player) {
        ESP_LOGE(DUEL_TAG, "Command received but player is null");
        return;
    }

    ESP_LOGI(DUEL_TAG, "Received command: %d", command.command);
    
    if(command.command == QDCommand::DRAW_RESULT) {
        hasReceivedDrawResult = true;
        ESP_LOGI(DUEL_TAG, "Received DRAW_RESULT command from opponent");
        
        long opponentTime = player->isHunter() ? 
            command.match.getBountyDrawTime() : 
            command.match.getHunterDrawTime();
            
        ESP_LOGI(DUEL_TAG, "Opponent reaction time: %ld ms", opponentTime);
        
        if (opponentTime < 0) {
            ESP_LOGW(DUEL_TAG, "Received invalid opponent time: %ld", opponentTime);
            return;
        }
        
        player->isHunter() ? 
        MatchManager::GetInstance()->setBountyDrawTime(command.match.getBountyDrawTime()) 
        : MatchManager::GetInstance()->setHunterDrawTime(command.match.getHunterDrawTime());

        if(hasPressedButton) {
            ESP_LOGI(DUEL_TAG, "Both players have acted - transitioning to result state");
            transitionToDuelResultState = true;
        } else {
            ESP_LOGI(DUEL_TAG, "Only opponent has acted - starting grace period: %d ms", 
                    DUEL_RESULT_GRACE_PERIOD);
            duelTimer.invalidate();
            duelTimer.setTimer(DUEL_RESULT_GRACE_PERIOD);
        }
    } else {
        ESP_LOGW(DUEL_TAG, "Received unexpected command in Duel state: %d", command.command);
    }
}

bool Duel::transitionToIdle() {
    if (transitionToIdleState) {
        ESP_LOGI(DUEL_TAG, "Transitioning to idle state");
    }
    return transitionToIdleState;
}

bool Duel::transitionToDuelResult() {
    if (transitionToDuelResultState) {
        ESP_LOGI(DUEL_TAG, "Transitioning to duel result state");
    }
    return transitionToDuelResultState;
}

void Duel::onStateDismounted(Device *PDN) {
    ESP_LOGI(DUEL_TAG, "Duel state dismounted - Cleanup");
    
    duelTimer.invalidate();
    ESP_LOGI(DUEL_TAG, "Duel timer invalidated");
    
    // Log state before reset
    ESP_LOGI(DUEL_TAG, "State before reset - transitionToDuelResult: %d, transitionToIdle: %d, receivedResult: %d, pressedButton: %d",
            transitionToDuelResultState, transitionToIdleState, hasReceivedDrawResult, hasPressedButton);
    
    PDN->removeButtonCallbacks(ButtonIdentifier::PRIMARY_BUTTON);
    PDN->removeButtonCallbacks(ButtonIdentifier::SECONDARY_BUTTON);

    transitionToDuelResultState = false;
    transitionToIdleState = false;
    hasReceivedDrawResult = false;
    hasPressedButton = false;
}
