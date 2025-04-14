#include "device/pdn.hpp"
#include "game/quickdraw-states.hpp"
#include "game/quickdraw.hpp"
#include "game/quickdraw-resources.hpp"
#include "wireless/esp-now-comms.hpp"
#include "game/match-manager.hpp"
#include <esp_log.h>

#define DUEL_RESULT_TAG "DUEL_RESULT"

DuelResult::DuelResult(Player* player, MatchManager* matchManager) : State(QuickdrawStateId::DUEL_RESULT), player(player), matchManager(matchManager) {
    ESP_LOGI(DUEL_RESULT_TAG, "Duel result state created for player %s (Hunter: %d)", 
             player->getUserID().c_str(), player->isHunter());
    this->matchManager = matchManager;
}

DuelResult::~DuelResult() {
    ESP_LOGI(DUEL_RESULT_TAG, "Duel result state destroyed");
    player = nullptr;
}

void DuelResult::onStateMounted(Device *PDN) {
    ESP_LOGI(DUEL_RESULT_TAG, "Duel result state mounted");

    if(matchManager->didWin()) {
        wonBattle = true;
    } else {
        captured = true;
    }

    PDN->setVibration(0);

    matchManager->finalizeMatch();

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

    PDN->removeButtonCallbacks(ButtonIdentifier::PRIMARY_BUTTON);
    PDN->removeButtonCallbacks(ButtonIdentifier::SECONDARY_BUTTON);

    QuickdrawWirelessManager::GetInstance()->clearCallbacks();
             
    wonBattle = false;
    captured = false;
    PDN->stopAnimation();
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

