#include "game/quickdraw-states.hpp"
#include "game/quickdraw-requests.hpp"
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

    PDN->invalidateScreen()
    ->setGlyphMode(FontMode::TEXT)
    ->drawText("Uploading matches...", 10, 20)
    ->render();

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

bool UploadMatchesState::transitionToSleep() {
    return transitionToSleepState;
}   

bool UploadMatchesState::transitionToPlayerRegistration() {
    return transitionToPlayerRegistrationState;
}

