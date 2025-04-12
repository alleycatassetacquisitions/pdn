#include "game/quickdraw-states.hpp"
#include "game/quickdraw-requests.hpp"
#include "game/quickdraw-resources.hpp"
#include <esp_log.h>

const string TAG = "UploadMatchesState";

UploadMatchesState::UploadMatchesState(Player* player, WirelessManager* wirelessManager, MatchManager* matchManager) : State(UPLOAD_MATCHES) {
    this->player = player;
    this->wirelessManager = wirelessManager;
    this->matchManager = matchManager;
}

UploadMatchesState::~UploadMatchesState() {
    player = nullptr;
    wirelessManager = nullptr;
    matchManager = nullptr;
}

void UploadMatchesState::onStateMounted(Device *PDN) {

    showLoadingGlyphs(PDN);

    matchesJson = String(matchManager->toJson().c_str());

    QuickdrawRequests::updateMatches(
        wirelessManager,
        matchesJson,
        [this](const String& jsonResponse) {
            ESP_LOGI(TAG, "Successfully updated matches: %s", 
                    jsonResponse.c_str());

            matchManager->clearStorage();
            
            if(player->getUserID() == FORCE_MATCH_UPLOAD) {
                transitionToPlayerRegistrationState = true;
            } else {
                transitionToSleepState = true;
            }
        },
        [this](const WirelessErrorInfo& error) {
            ESP_LOGE(TAG, "Failed to update matches: %s (code: %d), willRetry: %d", 
                error.message.c_str(), static_cast<int>(error.code), error.willRetry);
            
            if(!error.willRetry) {
                uploadMatchesTimer.setTimer(UPLOAD_MATCHES_RETRY_DELAY);
            }
        }
    );
}

void UploadMatchesState::onStateLoop(Device *PDN) {
    if(uploadMatchesTimer.expired()) {
        retryMatchUpload();
    }

    showLoadingGlyphs(PDN);
}

void UploadMatchesState::onStateDismounted(Device *PDN) {
    uploadMatchesTimer.invalidate();
    matchUploadRetryCount = 0;
    transitionToSleepState = false;
    transitionToPlayerRegistrationState = false;
}

void UploadMatchesState::retryMatchUpload() {
    if(matchUploadRetryCount < UPLOAD_MATCHES_MAX_RETRIES) {
        matchUploadRetryCount++;
        QuickdrawRequests::updateMatches(
            wirelessManager,
            matchesJson,
            [this](const String& jsonResponse) {
                ESP_LOGI(TAG, "Successfully updated matches: %s", 
                        jsonResponse.c_str());

                matchManager->clearStorage();
                
                if(player->getUserID() == FORCE_MATCH_UPLOAD) {
                    transitionToPlayerRegistrationState = true;
                } else {
                    transitionToSleepState = true;
                }
            },
            [this](const WirelessErrorInfo& error) {
                ESP_LOGE(TAG, "Failed to update matches: %s (code: %d), willRetry: %d", 
                    error.message.c_str(), static_cast<int>(error.code), error.willRetry);
                
                if(!error.willRetry) {
                    uploadMatchesTimer.setTimer(UPLOAD_MATCHES_RETRY_DELAY);
                }
            }
        );
    } else {
        ESP_LOGE(TAG, "Failed to upload matches after %d retries", matchUploadRetryCount);
        if(player->getUserID() == FORCE_MATCH_UPLOAD) {
            transitionToPlayerRegistrationState = true;
        } else {
            transitionToSleepState = true;
        }
    }
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
    return transitionToSleepState;
}   

bool UploadMatchesState::transitionToPlayerRegistration() {
    return transitionToPlayerRegistrationState;
}

