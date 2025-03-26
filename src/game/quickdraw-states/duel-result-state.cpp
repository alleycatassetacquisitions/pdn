#include "device/pdn.hpp"
#include "game/quickdraw-states.hpp"
#include "game/quickdraw.hpp"
#include "game/quickdraw-resources.hpp"
#include "wireless/esp-now-comms.hpp"
#include "game/match-manager.hpp"
#include <esp_log.h>

#define DUEL_RESULT_TAG "DUEL_RESULT"

DuelResult::DuelResult(Player* player) : State(QuickdrawStateId::DUEL_RESULT), player(player) {
    ESP_LOGI(DUEL_RESULT_TAG, "Duel result state created for player %s (Hunter: %d)", 
             player->getUserID().c_str(), player->isHunter());
}

DuelResult::~DuelResult() {
    ESP_LOGI(DUEL_RESULT_TAG, "Duel result state destroyed");
    player = nullptr;
}

void DuelResult::onStateMounted(Device *PDN) {
    ESP_LOGI(DUEL_RESULT_TAG, "Duel result state mounted");
    
    unsigned long drawTime = player->isHunter() ? 
        MatchManager::GetInstance()->getCurrentMatch()->getHunterDrawTime() : 
        MatchManager::GetInstance()->getCurrentMatch()->getBountyDrawTime();
    unsigned long opponentDrawTime = player->isHunter() ? 
        MatchManager::GetInstance()->getCurrentMatch()->getBountyDrawTime() : 
        MatchManager::GetInstance()->getCurrentMatch()->getHunterDrawTime();

    ESP_LOGI(DUEL_RESULT_TAG, "Draw times - Player: %lu ms, Opponent: %lu ms", 
             drawTime, opponentDrawTime);

    if(drawTime == DUEL_NO_RESULT_TIME || opponentDrawTime == DUEL_NO_RESULT_TIME) {
        ESP_LOGW(DUEL_RESULT_TAG, "Invalid draw time detected - marking as loss");
        captured = true;
    } else if(drawTime < opponentDrawTime) {
        ESP_LOGI(DUEL_RESULT_TAG, "Player won with faster reaction time");
        wonBattle = true;
    } else {
        ESP_LOGI(DUEL_RESULT_TAG, "Player lost with slower reaction time");
        captured = true;
    }

    PDN->invalidateScreen()->render();
}

void DuelResult::onStateLoop(Device *PDN) {
    // No loop processing needed for result state
}

void DuelResult::onStateDismounted(Device *PDN) {
    ESP_LOGI(DUEL_RESULT_TAG, "Duel result state dismounted - Cleaning up");
    
    // Log state before reset
    ESP_LOGI(DUEL_RESULT_TAG, "State before reset - wonBattle: %d, captured: %d",
             wonBattle, captured);
             
    wonBattle = false;
    captured = false;
}

bool DuelResult::transitionToWin() {
    if (wonBattle) {
        ESP_LOGI(DUEL_RESULT_TAG, "Transitioning to win state");
    }
    return wonBattle;
}

bool DuelResult::transitionToLose() {
    if (captured) {
        ESP_LOGI(DUEL_RESULT_TAG, "Transitioning to lose state");
    }
    return captured;
}

