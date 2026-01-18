#include "game/quickdraw-states.hpp"
#include "game/quickdraw-requests.hpp"
#include "game/quickdraw-resources.hpp"
#include "device/drivers/logger.hpp"

static const char* TAG = "UploadMatchesState";

UploadMatchesState::UploadMatchesState(Player* player, HttpClientInterface* httpClient, MatchManager* matchManager) : State(UPLOAD_MATCHES) {
    this->player = player;
    this->httpClient = httpClient;
    this->matchManager = matchManager;
    LOG_I(TAG, "UploadMatchesState initialized");
}

UploadMatchesState::~UploadMatchesState() {
    LOG_I(TAG, "UploadMatchesState destroyed");
    player = nullptr;
    httpClient = nullptr;
    matchManager = nullptr;
}

void UploadMatchesState::attemptUpload() {
    QuickdrawRequests::updateMatches(
        httpClient,
        matchesJson,
        [this](const std::string& jsonResponse) {
            LOG_I(TAG, "Successfully updated matches: %s", jsonResponse.c_str());
            matchManager->clearStorage();
            routeToNextState();
        },
        [this](const WirelessErrorInfo& error) {
            LOG_E(TAG, "Failed to update matches: %s (code: %d)", 
                error.message.c_str(), static_cast<int>(error.code));
            
            // Try to reconnect and retry the upload
            if (matchUploadRetryCount < 3) {
                matchUploadRetryCount++;
                LOG_I(TAG, "Retrying upload attempt %d/3", matchUploadRetryCount);
                httpClient->retryConnection();
                uploadMatchesTimer.setTimer(UPLOAD_MATCHES_TIMEOUT);
                shouldRetryUpload = true;
            } else {
                LOG_W(TAG, "Max upload retries reached. Proceeding without upload.");
                routeToNextState();
            }
        }
    );
}

void UploadMatchesState::onStateMounted(Device *PDN) {
    LOG_I(TAG, "State mounted - Starting match upload process");

    showLoadingGlyphs(PDN);
    uploadMatchesTimer.setTimer(UPLOAD_MATCHES_TIMEOUT);
    matchUploadRetryCount = 0;

    matchesJson = matchManager->toJson();
    LOG_I(TAG, "Match data prepared for upload: %d bytes", matchesJson.length());

    attemptUpload();

    AnimationConfig config;
    config.type = AnimationType::TRANSMIT_BREATH;
    config.loop = true;
    config.speed = 10;
    config.initialState = LEDState();
    config.initialState.transmitLight = LEDState::SingleLEDState(LEDColor(bountyColors[0].red, bountyColors[0].green, bountyColors[0].blue), 255);
    PDN->getLightManager()->startAnimation(config);
}

void UploadMatchesState::onStateLoop(Device *PDN) {
    uploadMatchesTimer.updateTime();
    
    // Check if we should retry the upload after connection is re-established
    if (shouldRetryUpload && httpClient->isConnected()) {
        LOG_I(TAG, "Connection re-established, retrying upload");
        shouldRetryUpload = false;
        attemptUpload();
    }
    
    if(uploadMatchesTimer.expired()) {
        LOG_W(TAG, "Upload timeout expired");
        routeToNextState();
    }

    showLoadingGlyphs(PDN);
}

void UploadMatchesState::onStateDismounted(Device *PDN) {
    LOG_I(TAG, "State dismounted");
    uploadMatchesTimer.invalidate();
    transitionToSleepState = false;
    transitionToPlayerRegistrationState = false;
    shouldRetryUpload = false;
    matchUploadRetryCount = 0;
    PDN->getLightManager()->stopAnimation();
}

void UploadMatchesState::showLoadingGlyphs(Device *PDN) {
    const int GLYPH_SIZE = 14;
    const int SCREEN_WIDTH = 128;
    const int SCREEN_HEIGHT = 64;
    
    const int GLYPHS_PER_ROW = (SCREEN_WIDTH / GLYPH_SIZE);
    const int GLYPHS_PER_COL = (SCREEN_HEIGHT - GLYPH_SIZE / GLYPH_SIZE);
    
    PDN->getDisplay()->invalidateScreen();
    PDN->getDisplay()->setGlyphMode(FontMode::LOADING_GLYPH);
    
    for (int row = 0; row < GLYPHS_PER_COL; row++) {
        for (int col = 0; col < GLYPHS_PER_ROW; col++) {
            if(rand() % 100 < 50) {
                int x = col * GLYPH_SIZE;
                int y = 14 + (row * GLYPH_SIZE);
                int randomIndex = rand() % 8;
                const char* glyph = loadingGlyphs[randomIndex];
                PDN->getDisplay()->renderGlyph(glyph, x, y);
            }
        }
    }
    
    PDN->getDisplay()->render();
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
