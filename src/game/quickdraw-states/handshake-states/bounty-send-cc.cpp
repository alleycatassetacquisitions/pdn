#include "game/quickdraw-states.hpp"
#include "id-generator.hpp"
#include "esp_log.h"

//
// Created by Elli Furedy on 9/30/2024.
//

#include "wireless/quickdraw-wireless-manager.hpp"
#include "game/match-manager.hpp"


BountySendConnectionConfirmedState::BountySendConnectionConfirmedState(Player *player) : BaseHandshakeState(BOUNTY_SEND_CC_STATE) {
    this->player = player;
}

BountySendConnectionConfirmedState::~BountySendConnectionConfirmedState() {
    player = nullptr;
}



void BountySendConnectionConfirmedState::onStateMounted(Device *PDN) {
    if (!player) {
        ESP_LOGE("BOUNTY_SEND_CC", "Player is null in onStateMounted");
        return;
    }

    if (!player->getOpponentMacAddress()) {
        ESP_LOGE("BOUNTY_SEND_CC", "Opponent MAC address is null");
        return;
    }

    ESP_LOGI("BOUNTY_SEND_CC", "State mounted");
    
    string bountyId = player->getUserID();
    if (bountyId.empty()) {
        ESP_LOGE("BOUNTY_SEND_CC", "Player ID is empty");
        return;
    }

    string matchId = IdGenerator::GetInstance()->generateId();
    if (matchId.empty()) {
        ESP_LOGE("BOUNTY_SEND_CC", "Failed to generate match ID");
        return;
    }
    
    ESP_LOGI("BOUNTY_SEND_CC", "Broadcasting packet with matchId: %s, bountyId: %s", 
             matchId.c_str(), bountyId.c_str());
    
    QuickdrawWirelessManager::GetInstance()->setPacketReceivedCallback(
        std::bind(&BountySendConnectionConfirmedState::onQuickdrawCommandReceived, this, std::placeholders::_1)
    );

    try {
        Match initialMatch(matchId, "", bountyId);  // Empty hunter ID initially
        QuickdrawWirelessManager::GetInstance()->broadcastPacket(
            *player->getOpponentMacAddress(),
            CONNECTION_CONFIRMED,
            initialMatch
        );
        ESP_LOGI("BOUNTY_SEND_CC", "Sent CONNECTION_CONFIRMED");
    } catch (const std::exception& e) {
        ESP_LOGE("BOUNTY_SEND_CC", "Failed to send CONNECTION_CONFIRMED: %s", e.what());
    }
}

void BountySendConnectionConfirmedState::onQuickdrawCommandReceived(QuickdrawCommand command) {
    if (!player) {
        ESP_LOGE("BOUNTY_SEND_CC", "Player is null in command handler");
        return;
    }

    if (!player->getOpponentMacAddress()) {
        ESP_LOGE("BOUNTY_SEND_CC", "Opponent MAC address is null");
        return;
    }

    ESP_LOGI("BOUNTY_SEND_CC", "Received command: %d", command.command);
    
    if (command.command == HUNTER_RECEIVE_MATCH) {
        ESP_LOGI("BOUNTY_SEND_CC", "Received HUNTER_RECEIVE_MATCH command");
        
        // Validate received match data
        if (command.match.getMatchId().empty()) {
            ESP_LOGE("BOUNTY_SEND_CC", "Received empty match ID");
            return;
        }
        if (command.match.getHunterId().empty()) {
            ESP_LOGE("BOUNTY_SEND_CC", "Received empty hunter ID");
            return;
        }
        if (command.match.getBountyId().empty()) {
            ESP_LOGE("BOUNTY_SEND_CC", "Received empty bounty ID");
            return;
        }

        ESP_LOGI("BOUNTY_SEND_CC", "Received match - ID: %s, Hunter: %s, Bounty: %s",
                 command.match.getMatchId().c_str(),
                 command.match.getHunterId().c_str(),
                 command.match.getBountyId().c_str());

        Match* match = MatchManager::GetInstance()->receiveMatch(command.match);
        if (!match) {
            ESP_LOGE("BOUNTY_SEND_CC", "Failed to receive match");
            return;
        }

        try {
            QuickdrawWirelessManager::GetInstance()->broadcastPacket(
                *player->getOpponentMacAddress(),
                BOUNTY_FINAL_ACK,
                *match
            );
            ESP_LOGI("BOUNTY_SEND_CC", "Sent BOUNTY_FINAL_ACK");
            transitionToConnectionSuccessfulState = true;
        } catch (const std::exception& e) {
            ESP_LOGE("BOUNTY_SEND_CC", "Failed to send BOUNTY_FINAL_ACK: %s", e.what());
        }
    } else {
        ESP_LOGW("BOUNTY_SEND_CC", "Received unexpected command: %d", command.command);
    }
}

void BountySendConnectionConfirmedState::onStateLoop(Device *PDN) {
    
}

void BountySendConnectionConfirmedState::onStateDismounted(Device *PDN) {
    transitionToConnectionSuccessfulState = false;
    ESP_LOGI("BOUNTY_SEND_CC", "State dismounted");
    BaseHandshakeState::resetTimeout();
    QuickdrawWirelessManager::GetInstance()->clearCallbacks();
}

bool BountySendConnectionConfirmedState::transitionToConnectionSuccessful() {
    return transitionToConnectionSuccessfulState;
}
