#include "game/quickdraw-states.hpp"
#include "game/quickdraw-resources.hpp"
#include "game/quickdraw-requests.hpp"
#include "device/pdn.hpp"
#include <esp_log.h>

// Logging tag for PlayerRegistration state
static const char* TAG = "PlayerRegistration";

PlayerRegistration::PlayerRegistration(Player* player) : State(QuickdrawStateId::PLAYER_REGISTRATION) {
    ESP_LOGI(TAG, "Initializing PlayerRegistration state");
    this->player = player;
}

PlayerRegistration::~PlayerRegistration() {
    player = nullptr;
    ESP_LOGI(TAG, "Destroying PlayerRegistration state");
}

void PlayerRegistration::onStateMounted(Device *PDN) {
    ESP_LOGI(TAG, "State mounted - Starting player registration");
    PDN->invalidateScreen()->
    setGlyphMode(FontMode::TEXT)->
    drawText("Pairing Code", 8, 16)->
    setGlyphMode(FontMode::NUMBER_GLYPH)->
    renderGlyph(digitGlyphs[0], 20, 40)->
    render();

    PDN->setButtonClick(ButtonInteraction::CLICK, ButtonIdentifier::PRIMARY_BUTTON, 
    [](void *ctx) {
        PlayerRegistration* playerRegistration = (PlayerRegistration*)ctx;
        playerRegistration->currentDigit++;
        if(playerRegistration->currentDigit > 9) {
            playerRegistration->currentDigit = 0;
        }
        ESP_LOGD(TAG, "Primary button clicked - Current digit cycled to: %d", playerRegistration->currentDigit);
        playerRegistration->shouldRender = true;
    }, this);

    PDN->setButtonClick(ButtonInteraction::CLICK, ButtonIdentifier::SECONDARY_BUTTON, 
    [](void *ctx) {
        PlayerRegistration* playerRegistration = (PlayerRegistration*)ctx;
        playerRegistration->inputId[playerRegistration->currentDigitIndex] = playerRegistration->currentDigit;
        ESP_LOGD(TAG, "Secondary button clicked - Digit %d set to %d", 
                 playerRegistration->currentDigitIndex, 
                 playerRegistration->currentDigit);

        playerRegistration->currentDigitIndex++;
        playerRegistration->currentDigit = 0;
        if(playerRegistration->currentDigitIndex >= playerRegistration->DIGIT_COUNT) {
            // Convert inputId array to string
            char playerId[5];
            snprintf(playerId, sizeof(playerId), "%d%d%d%d", 
                    playerRegistration->inputId[0],
                    playerRegistration->inputId[1],
                    playerRegistration->inputId[2],
                    playerRegistration->inputId[3]);
            ESP_LOGI(TAG, "Player registration complete - ID: %s", playerId);
            playerRegistration->player->setUserID(playerId);
            playerRegistration->transitionToUserFetchState = true;
        } else {
            playerRegistration->shouldRender = true;
        }
    }, this);
}

void PlayerRegistration::onStateLoop(Device *PDN) {
    if(shouldRender) {
        ESP_LOGD(TAG, "Rendering display - Digit index: %d, Current digit: %d", currentDigitIndex, currentDigit);
        if(currentDigitIndex == 0) {
            PDN->
            invalidateScreen()->
            setGlyphMode(FontMode::TEXT)->
            drawText("Pairing Code", 8, 16)->
            setGlyphMode(FontMode::NUMBER_GLYPH)->
            renderGlyph(digitGlyphs[currentDigit], 20, 40)->
            render();
        } else if(currentDigitIndex == 1) {
            PDN->
            invalidateScreen()->
            setGlyphMode(FontMode::TEXT)->
            drawText("Pairing Code", 8, 16)->
            setGlyphMode(FontMode::NUMBER_GLYPH)->
            renderGlyph(digitGlyphs[inputId[0]], 20, 40)->
            renderGlyph(digitGlyphs[currentDigit], 44, 40)->
            render();
        } else if(currentDigitIndex == 2) {
            PDN->
            invalidateScreen()->
            setGlyphMode(FontMode::TEXT)->
            drawText("Pairing Code", 8, 16)->
            setGlyphMode(FontMode::NUMBER_GLYPH)->
            renderGlyph(digitGlyphs[inputId[0]], 20, 40)->
            renderGlyph(digitGlyphs[inputId[1]], 44, 40)->
            renderGlyph(digitGlyphs[currentDigit], 68, 40)->
            render();
        } else if(currentDigitIndex == 3) {
            PDN->
            invalidateScreen()->
            setGlyphMode(FontMode::TEXT)->
            drawText("Pairing Code", 8, 16)->
            setGlyphMode(FontMode::NUMBER_GLYPH)->
            renderGlyph(digitGlyphs[inputId[0]], 20, 40)->
            renderGlyph(digitGlyphs[inputId[1]], 44, 40)->
            renderGlyph(digitGlyphs[inputId[2]], 68, 40)->
            renderGlyph(digitGlyphs[currentDigit], 92, 40)->
            render();
        }
        shouldRender = false;
    }
}

void PlayerRegistration::onStateDismounted(Device *PDN) {
    ESP_LOGI(TAG, "State dismounted - Cleaning up");
    PDN->setGlyphMode(FontMode::TEXT);
    PDN->removeButtonCallbacks(ButtonIdentifier::PRIMARY_BUTTON);
    PDN->removeButtonCallbacks(ButtonIdentifier::SECONDARY_BUTTON);
    //reset all member variables
    currentDigitIndex = 0;
    currentDigit = 0;
    transitionToUserFetchState = false;
    ESP_LOGI(TAG, "State cleanup complete");
}

bool PlayerRegistration::transitionToUserFetch() {
    return transitionToUserFetchState;
}


