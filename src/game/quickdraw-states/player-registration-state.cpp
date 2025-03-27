#include "game/quickdraw-states.hpp"
#include "game/quickdraw-resources.hpp"
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
    PDN->drawText("Pairing Code", 16, 16)->
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
            playerRegistration->isFetchingUserData = true;
        } else {
            playerRegistration->shouldRender = true;
        }
    }, this);
}

void PlayerRegistration::onStateLoop(Device *PDN) {

    if(isFetchingUserData) {
        ESP_LOGD(TAG, "Fetching user data");
        if(!userDataFetchTimer.isRunning()) {
            ESP_LOGD(TAG, "User data fetch timer is not running");
            userDataFetchTimer.setTimer(USER_DATA_FETCH_TIMEOUT);
        } else if(userDataFetchTimer.expired()) {
            ESP_LOGD(TAG, "User data fetch timer expired");
        } else {
            EVERY_N_MILLISECONDS(50) {
                showLoadingGlyphs(PDN);
            }
        }
    } else if(shouldRender) {
        ESP_LOGD(TAG, "Rendering display - Digit index: %d, Current digit: %d", currentDigitIndex, currentDigit);
        if(currentDigitIndex == 0) {
            PDN->
            invalidateScreen()->
            setGlyphMode(FontMode::TEXT)->
            drawText("Pairing Code", 16, 16)->
            setGlyphMode(FontMode::NUMBER_GLYPH)->
            renderGlyph(digitGlyphs[currentDigit], 20, 40)->
            render();
        } else if(currentDigitIndex == 1) {
            PDN->
            invalidateScreen()->
            setGlyphMode(FontMode::TEXT)->
            drawText("Pairing Code", 16, 16)->
            setGlyphMode(FontMode::NUMBER_GLYPH)->
            renderGlyph(digitGlyphs[inputId[0]], 20, 40)->
            renderGlyph(digitGlyphs[currentDigit], 44, 40)->
            render();
        } else if(currentDigitIndex == 2) {
            PDN->
            invalidateScreen()->
            setGlyphMode(FontMode::TEXT)->
            drawText("Pairing Code", 16, 16)->
            setGlyphMode(FontMode::NUMBER_GLYPH)->
            renderGlyph(digitGlyphs[inputId[0]], 20, 40)->
            renderGlyph(digitGlyphs[inputId[1]], 44, 40)->
            renderGlyph(digitGlyphs[currentDigit], 68, 40)->
            render();
        } else if(currentDigitIndex == 3) {
            PDN->
            invalidateScreen()->
            setGlyphMode(FontMode::TEXT)->
            drawText("Pairing Code", 16, 16)->
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

void PlayerRegistration::showLoadingGlyphs(Device *PDN) {
    ESP_LOGD(TAG, "Showing loading glyphs");
    
    // Calculate grid layout
    const int GLYPH_SIZE = 14;  // Each glyph is 14x14 pixels
    const int SCREEN_WIDTH = 128;
    const int SCREEN_HEIGHT = 64;
    
    // Calculate number of glyphs that can fit in each dimension
    const int GLYPHS_PER_ROW = (SCREEN_WIDTH / GLYPH_SIZE);
    const int GLYPHS_PER_COL = (SCREEN_HEIGHT - GLYPH_SIZE / GLYPH_SIZE);  // Start at y=14
    
    // Clear the screen
    PDN->invalidateScreen();
    
    // Set glyph mode for rendering
    PDN->setGlyphMode(FontMode::LOADING_GLYPH);
    
    // Fill the screen with random loading glyphs
    for (int row = 0; row < GLYPHS_PER_COL; row++) {
        for (int col = 0; col < GLYPHS_PER_ROW; col++) {
            if(random(0, 100) < 50) {
                // Calculate position
                int x = col * GLYPH_SIZE;
                int y = 14 + (row * GLYPH_SIZE);  // Start at y=14
                
                // Select random glyph from the array
                int randomIndex = random(0, 8);  // 8 glyphs in the array
                const char* glyph = loadingGlyphs[randomIndex];
                
                // Render the glyph
                PDN->renderGlyph(glyph, x, y);
            }
        }
    }
    
    // Final render call
    PDN->render();
    ESP_LOGD(TAG, "Loading glyphs rendered");
}

void PlayerRegistration::convertInputIdToString() {
    char playerId[5];
    snprintf(playerId, sizeof(playerId), "%d%d%d%d", 
            inputId[0],
            inputId[1],
            inputId[2],
            inputId[3]);
    player->setUserID(playerId);
}
void PlayerRegistration::onStateDismounted(Device *PDN) {
    ESP_LOGI(TAG, "State dismounted - Cleaning up");
    PDN->setGlyphMode(FontMode::TEXT);
    PDN->removeButtonCallbacks(ButtonIdentifier::PRIMARY_BUTTON);
    PDN->removeButtonCallbacks(ButtonIdentifier::SECONDARY_BUTTON);
    //reset all member variables
    currentDigitIndex = 0;
    currentDigit = 1;
    ESP_LOGI(TAG, "State cleanup complete");
}

bool PlayerRegistration::transitionToSleep() {
    return false;
}


