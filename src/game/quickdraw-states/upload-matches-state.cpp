#include "game/quickdraw-states.hpp"
#include "game/quickdraw-requests.hpp"
#include "game/quickdraw-resources.hpp"
#include <esp_log.h>

static const char* TAG = "UploadMatchesState";

UploadMatchesState::UploadMatchesState(Player* player, WirelessManager* wirelessManager, MatchManager* matchManager) : State(UPLOAD_MATCHES) {
    this->player = player;
    this->wirelessManager = wirelessManager;
    this->matchManager = matchManager;
    ESP_LOGI(TAG, "UploadMatchesState initialized");
}

UploadMatchesState::~UploadMatchesState() {
    ESP_LOGI(TAG, "UploadMatchesState destroyed");
    player = nullptr;
    wirelessManager = nullptr;
    matchManager = nullptr;
}

void UploadMatchesState::onStateMounted(Device *PDN) {
    ESP_LOGI(TAG, "State mounted - Starting match upload process");

    showLoadingGlyphs(PDN);
    uploadMatchesTimer.setTimer(UPLOAD_MATCHES_TIMEOUT);

    matchesJson = matchManager->toJson();
    ESP_LOGI(TAG, "Match data prepared for upload: %d bytes", matchesJson.length());

    QuickdrawRequests::updateMatches(
        wirelessManager,
        matchesJson,
        [this](const std::string& jsonResponse) {
            ESP_LOGI(TAG, "Successfully updated matches: %s", 
                    jsonResponse.c_str());

            matchManager->clearStorage();
            ESP_LOGI(TAG, "Match storage cleared after successful upload");
            routeToNextState();
            
        },
        [this](const WirelessErrorInfo& error) {
            ESP_LOGE(TAG, "Failed to update matches: %s (code: %d), willRetry: %d", 
                error.message.c_str(), static_cast<int>(error.code), error.willRetry);
            routeToNextState();
        }
    );

    AnimationConfig config;
    config.type = AnimationType::TRANSMIT_BREATH;
    config.loop = true;
    config.speed = 10;
    config.initialState = LEDState();
    config.initialState.transmitLight = LEDState::SingleLEDState(LEDColor(bountyColors[0].red, bountyColors[0].green, bountyColors[0].blue), 255);
    PDN->startAnimation(config);
}

void UploadMatchesState::onStateLoop(Device *PDN) {
    uploadMatchesTimer.updateTime();
    if(uploadMatchesTimer.expired()) {
        ESP_LOGI(TAG, "Retry timer expired, attempting match upload again");
        routeToNextState();
    }

    showLoadingGlyphs(PDN);
}

void UploadMatchesState::onStateDismounted(Device *PDN) {
    ESP_LOGI(TAG, "State dismounted - Cleaning up resources");
    uploadMatchesTimer.invalidate();
    transitionToSleepState = false;
    transitionToPlayerRegistrationState = false;
    PDN->stopAnimation();
}

void UploadMatchesState::showLoadingGlyphs(Device *PDN) {
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

bool UploadMatchesState::transitionToSleep() {
    if(transitionToSleepState) {
        ESP_LOGI(TAG, "Transitioning to Sleep state");
    }
    return transitionToSleepState;
}   

bool UploadMatchesState::transitionToPlayerRegistration() {
    if(transitionToPlayerRegistrationState) {
        ESP_LOGI(TAG, "Transitioning to Player Registration state");
    }
    return transitionToPlayerRegistrationState;
}

void UploadMatchesState::routeToNextState() {
    if(player->getUserID() == FORCE_MATCH_UPLOAD) {
        ESP_LOGI(TAG, "FORCE_MATCH_UPLOAD user ID detected - transitioning to player registration");
        transitionToPlayerRegistrationState = true;
    } else {
        ESP_LOGI(TAG, "Regular user - transitioning to sleep state");
        transitionToSleepState = true;
    }
}