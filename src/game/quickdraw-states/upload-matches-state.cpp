#include "game/quickdraw-states.hpp"
#include "game/quickdraw-requests.hpp"
#include "game/quickdraw-resources.hpp"
#include <esp_log.h>

static const char* TAG = "UploadMatchesState";

UploadMatchesState::UploadMatchesState(Player* player, HttpClientInterface* httpClient, MatchManager* matchManager) : State(UPLOAD_MATCHES) {
    this->player = player;
    this->httpClient = httpClient;
    this->matchManager = matchManager;
    ESP_LOGI(TAG, "UploadMatchesState initialized");
}

UploadMatchesState::~UploadMatchesState() {
    ESP_LOGI(TAG, "UploadMatchesState destroyed");
    player = nullptr;
    httpClient = nullptr;
    matchManager = nullptr;
}

void UploadMatchesState::onStateMounted(Device *PDN) {
    ESP_LOGI(TAG, "State mounted - Starting match upload process");

    showLoadingGlyphs(PDN);
    uploadMatchesTimer.setTimer(UPLOAD_MATCHES_TIMEOUT);

    matchesJson = matchManager->toJson();
    ESP_LOGI(TAG, "Match data prepared for upload: %d bytes", matchesJson.length());

    QuickdrawRequests::updateMatches(
        httpClient,
        matchesJson,
        [this](const std::string& jsonResponse) {
            ESP_LOGI(TAG, "Successfully updated matches: %s", jsonResponse.c_str());
            matchManager->clearStorage();
            routeToNextState();
        },
        [this](const WirelessErrorInfo& error) {
            ESP_LOGE(TAG, "Failed to update matches: %s (code: %d)", 
                error.message.c_str(), static_cast<int>(error.code));
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
        ESP_LOGI(TAG, "Retry timer expired");
        routeToNextState();
    }

    showLoadingGlyphs(PDN);
}

void UploadMatchesState::onStateDismounted(Device *PDN) {
    ESP_LOGI(TAG, "State dismounted");
    uploadMatchesTimer.invalidate();
    transitionToSleepState = false;
    transitionToPlayerRegistrationState = false;
    PDN->stopAnimation();
}

void UploadMatchesState::showLoadingGlyphs(Device *PDN) {
    const int GLYPH_SIZE = 14;
    const int SCREEN_WIDTH = 128;
    const int SCREEN_HEIGHT = 64;
    
    const int GLYPHS_PER_ROW = (SCREEN_WIDTH / GLYPH_SIZE);
    const int GLYPHS_PER_COL = (SCREEN_HEIGHT - GLYPH_SIZE / GLYPH_SIZE);
    
    PDN->invalidateScreen();
    PDN->setGlyphMode(FontMode::LOADING_GLYPH);
    
    for (int row = 0; row < GLYPHS_PER_COL; row++) {
        for (int col = 0; col < GLYPHS_PER_ROW; col++) {
            if(random(0, 100) < 50) {
                int x = col * GLYPH_SIZE;
                int y = 14 + (row * GLYPH_SIZE);
                int randomIndex = random(0, 8);
                const char* glyph = loadingGlyphs[randomIndex];
                PDN->renderGlyph(glyph, x, y);
            }
        }
    }
    
    PDN->render();
}

bool UploadMatchesState::transitionToSleep() {
    return transitionToSleepState;
}   

bool UploadMatchesState::transitionToPlayerRegistration() {
    return transitionToPlayerRegistrationState;
}

void UploadMatchesState::routeToNextState() {
    if(player->getUserID() == FORCE_MATCH_UPLOAD) {
        transitionToPlayerRegistrationState = true;
    } else {
        transitionToSleepState = true;
    }
}
