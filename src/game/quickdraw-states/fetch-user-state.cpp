#include "game/quickdraw-states.hpp"
#include "game/quickdraw-resources.hpp"
#include "game/quickdraw-requests.hpp"
#include "device/pdn.hpp"
#include <esp_log.h>

static const char* TAG = "FetchUserDataState";

FetchUserDataState::FetchUserDataState(Player* player, WirelessManager* wirelessManager) : State(QuickdrawStateId::FETCH_USER_DATA) {
    ESP_LOGI(TAG, "Initializing FetchUserDataState");
    this->player = player;
    this->wirelessManager = wirelessManager;
    // Set a longer fetch timeout (20 seconds)
}   

FetchUserDataState::~FetchUserDataState() {
    ESP_LOGI(TAG, "Destroying FetchUserDataState");
}   

void FetchUserDataState::onStateMounted(Device *PDN) {
    ESP_LOGI(TAG, "State mounted - Starting user data fetch");
    showLoadingGlyphs(PDN);
    isFetchingUserData = true;
    userDataFetchTimer.setTimer(20000);
    
    // Log important information
    ESP_LOGI(TAG, "Player ID for fetch: %s", player->getUserID().c_str());
    ESP_LOGI(TAG, "WiFi state: %d", wirelessManager->getCurrentState()->getStateId());
    
    QuickdrawRequests::getPlayer(
        wirelessManager,
        String(player->getUserID().c_str()),
        [this](const PlayerResponse& response) {
            ESP_LOGI(TAG, "Successfully fetched player data: %s (%s)", 
                    response.name.c_str(), response.id.c_str());
            
            // Set player information using the setters
            player->setName(response.name.c_str());
            player->setIsHunter(response.isHunter);
            player->setAllegiance(response.allegiance.c_str());
            player->setFaction(response.faction.c_str());

            renderWelcomeMessage();
            userDataFetchTimer.invalidate();
            isFetchingUserData = false;
        },
        [this](const WirelessErrorInfo& error) {
            ESP_LOGE(TAG, "Failed to fetch player data: %s (code: %d)", 
                   error.message.c_str(), static_cast<int>(error.code));
            resetToPlayerRegistrationState = true;
        }
    );
}   

void FetchUserDataState::onStateLoop(Device *PDN) {
    userDataFetchTimer.updateTime();
    
    // Log fetch progress periodically
    static unsigned long lastLogTime = 0;
    if (millis() - lastLogTime > 5000) {
        ESP_LOGI(TAG, "FetchUserDataState - still waiting for response, timer: %lu ms", 
                 userDataFetchTimer.getElapsedTime());
        lastLogTime = millis();
    }

    if(userDataFetchTimer.expired()) {
        ESP_LOGW(TAG, "User data fetch timer expired after %lu ms", userDataFetchTimer.getElapsedTime());
        isFetchingUserData = false;
        resetToPlayerRegistrationState = true;
    } else if(userDataFetchTimer.isRunning()) {
        EVERY_N_MILLISECONDS(50) {
            showLoadingGlyphs(PDN);
        }
    }
}   

void FetchUserDataState::onStateDismounted(Device *PDN) {
    ESP_LOGI(TAG, "State dismounted - Cleaning up");
    PDN->setGlyphMode(FontMode::TEXT);
    //reset all member variables
    isFetchingUserData = false;
    resetToPlayerRegistrationState = false;
    userDataFetchTimer.invalidate();
    ESP_LOGI(TAG, "State cleanup complete");
}   

bool FetchUserDataState::resetToPlayerRegistration() {
    static bool lastResetState = false;
    
    // Only log when the state actually changes
    if(resetToPlayerRegistrationState && !lastResetState) {
        ESP_LOGI(TAG, "Resetting to PlayerRegistration state");
    }
    
    lastResetState = resetToPlayerRegistrationState;
    return resetToPlayerRegistrationState;
}  

void FetchUserDataState::showLoadingGlyphs(Device *PDN) {
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
}  

void FetchUserDataState::renderWelcomeMessage() {
    PDN::GetInstance()->
    invalidateScreen()->
    setGlyphMode(FontMode::TEXT)->
    drawText("Welcome", 0, 16)->
    drawText(player->getName().c_str(), 0, 32)->
    drawText("to Quickdraw!", 0, 48)->
    render();
}








