#include "game/quickdraw-states.hpp"
#include "device/device.hpp"
#include "game/quickdraw-requests.hpp"
#include "game/quickdraw-resources.hpp"
#include "game/fdn-result-manager.hpp"
#include "device/drivers/logger.hpp"

static const char* TAG = "UploadMatchesState";

UploadMatchesState::UploadMatchesState(Player* player, WirelessManager* wirelessManager, MatchManager* matchManager, FdnResultManager* fdnResultManager) : State(UPLOAD_MATCHES) {
    this->player = player;
    this->wirelessManager = wirelessManager;
    this->matchManager = matchManager;
    this->fdnResultManager = fdnResultManager;
    LOG_I(TAG, "UploadMatchesState initialized");
}

UploadMatchesState::~UploadMatchesState() {
    LOG_I(TAG, "UploadMatchesState destroyed");
    player = nullptr;
    wirelessManager = nullptr;
    matchManager = nullptr;
    fdnResultManager = nullptr;
}

void UploadMatchesState::attemptUpload() {
    QuickdrawRequests::updateMatches(
        wirelessManager,
        matchesJson,
        [this](const std::string& jsonResponse) {
            LOG_I(TAG, "Successfully updated matches: %s", jsonResponse.c_str());
            matchManager->clearStorage();
            matchesUploaded = true;

            // Now attempt FDN upload if there are cached results
            if (fdnResultManager && fdnResultManager->getCachedResultCount() > 0) {
                attemptFdnUpload();
            } else {
                routeToNextState();
            }
        },
        [this](const WirelessErrorInfo& error) {
            LOG_E(TAG, "Failed to update matches: %s (code: %d)",
                error.message.c_str(), static_cast<int>(error.code));

            // Retry the upload - WirelessManager handles mode switching automatically
            if (matchUploadRetryCount < 3) {
                matchUploadRetryCount++;
                LOG_I(TAG, "Retrying upload attempt %d/3", matchUploadRetryCount);
                uploadMatchesTimer.setTimer(UPLOAD_MATCHES_TIMEOUT);
                shouldRetryUpload = true;
            } else {
                LOG_W(TAG, "Max upload retries reached. Proceeding without upload.");
                matchesUploaded = true;
                // Still try FDN upload even if matches failed
                if (fdnResultManager && fdnResultManager->getCachedResultCount() > 0) {
                    attemptFdnUpload();
                } else {
                    routeToNextState();
                }
            }
        }
    );
}

void UploadMatchesState::attemptFdnUpload() {
    if (!fdnResultManager) {
        routeToNextState();
        return;
    }

    uint8_t count = fdnResultManager->getCachedResultCount();
    if (count == 0) {
        routeToNextState();
        return;
    }

    // Build JSON array of results
    JsonDocument doc;
    JsonArray results = doc["results"].to<JsonArray>();

    for (uint8_t i = 0; i < count; i++) {
        std::string cached = fdnResultManager->getCachedResult(i);
        if (cached.empty()) continue;

        // Parse "gameType:won:score:hardMode"
        size_t pos1 = cached.find(':');
        size_t pos2 = cached.find(':', pos1 + 1);
        size_t pos3 = cached.find(':', pos2 + 1);

        if (pos1 == std::string::npos || pos2 == std::string::npos || pos3 == std::string::npos) {
            LOG_W(TAG, "Invalid cached result format: %s", cached.c_str());
            continue;
        }

        int gameType = std::stoi(cached.substr(0, pos1));
        bool won = (cached.substr(pos1 + 1, pos2 - pos1 - 1) == "1");
        int score = std::stoi(cached.substr(pos2 + 1, pos3 - pos2 - 1));
        bool hardMode = (cached.substr(pos3 + 1) == "1");

        JsonObject result = results.add<JsonObject>();
        result["gameType"] = gameType;
        result["won"] = won;
        result["score"] = score;
        result["difficulty"] = hardMode ? "hard" : "easy";
    }

    std::string resultsJson;
    serializeJson(doc, resultsJson);
    LOG_I(TAG, "FDN results JSON: %s", resultsJson.c_str());

    QuickdrawRequests::uploadFdnResults(
        wirelessManager,
        resultsJson,
        [this](const std::string& jsonResponse) {
            LOG_I(TAG, "Successfully uploaded FDN results: %s", jsonResponse.c_str());
            fdnResultManager->clearCachedResults();
            fdnResultsUploaded = true;
            routeToNextState();
        },
        [this](const WirelessErrorInfo& error) {
            LOG_E(TAG, "Failed to upload FDN results: %s (code: %d)",
                error.message.c_str(), static_cast<int>(error.code));

            // Retry the upload
            if (fdnUploadRetryCount < 3) {
                fdnUploadRetryCount++;
                LOG_I(TAG, "Retrying FDN upload attempt %d/3", fdnUploadRetryCount);
                uploadMatchesTimer.setTimer(UPLOAD_MATCHES_TIMEOUT);
                shouldRetryFdnUpload = true;
            } else {
                LOG_W(TAG, "Max FDN upload retries reached. Proceeding without upload.");
                fdnResultsUploaded = true;
                routeToNextState();
            }
        }
    );
}

void UploadMatchesState::onStateMounted(Device *PDN) {
    LOG_I(TAG, "State mounted - Starting match upload process");
    
    // Switch to WiFi mode for HTTP upload
    PDN->getWirelessManager()->enableWifiMode();

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
    if (shouldRetryUpload && wirelessManager->isWifiConnected()) {
        LOG_I(TAG, "Connection re-established, retrying match upload");
        shouldRetryUpload = false;
        attemptUpload();
    }

    // Check if we should retry FDN upload
    if (shouldRetryFdnUpload && wirelessManager->isWifiConnected()) {
        LOG_I(TAG, "Connection re-established, retrying FDN upload");
        shouldRetryFdnUpload = false;
        attemptFdnUpload();
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
    shouldRetryFdnUpload = false;
    matchUploadRetryCount = 0;
    fdnUploadRetryCount = 0;
    matchesUploaded = false;
    fdnResultsUploaded = false;
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
