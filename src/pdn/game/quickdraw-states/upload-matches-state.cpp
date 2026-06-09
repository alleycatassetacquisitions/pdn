#include "game/quickdraw-states.hpp"
#include "device/device.hpp"
#include "game/quickdraw-requests.hpp"
#include "game/quickdraw-resources.hpp"
#include "device/animation/transmit-breath-animation.hpp"
#include "device/drivers/logger.hpp"

static const char* TAG = "UploadMatchesState";

UploadMatchesState::UploadMatchesState(Player* player, WirelessManager* wirelessManager, MatchManager* matchManager) : TypedState<PDN>(UPLOAD_MATCHES) {
    this->player = player;
    this->wirelessManager = wirelessManager;
    this->matchManager = matchManager;
    LOG_I(TAG, "UploadMatchesState initialized");
}

UploadMatchesState::~UploadMatchesState() {
    LOG_I(TAG, "UploadMatchesState destroyed");
    player = nullptr;
    wirelessManager = nullptr;
    matchManager = nullptr;
}

void UploadMatchesState::attemptUpload() {
    QuickdrawRequests::updateMatches(
        wirelessManager,
        matchesJson,
        [this](const std::string& jsonResponse) {
            LOG_I(TAG, "Successfully updated matches: %s", jsonResponse.c_str());
            matchManager->clearStorage();
            transitionToSleepState = true;
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
                transitionToSleepState = true;
            }
        }
    );
}

void UploadMatchesState::onStateMounted(PDN* pdn) {
    LOG_I(TAG, "State mounted - Starting match upload process");
    
    // Switch to WiFi mode for HTTP upload
    pdn->getWirelessManager()->enableWifiMode();

    showLoadingGlyphs(pdn);
    uploadMatchesTimer.setTimer(UPLOAD_MATCHES_TIMEOUT);
    matchUploadRetryCount = 0;

    matchesJson = matchManager->toJson();
    LOG_I(TAG, "Match data prepared for upload: %d bytes", matchesJson.length());

    attemptUpload();

    AnimationConfig config;
    config.loop = true;
    config.speed = 10;
    config.initialState = LEDState();
    config.initialState.transmitLight = LEDState::SingleLEDState(LEDColor(bountyColors[0].red, bountyColors[0].green, bountyColors[0].blue), 255);
    pdn->getLightManager()->startAnimation(new TransmitBreathAnimation(), config);
}

void UploadMatchesState::onStateLoop(PDN* pdn) {
    uploadMatchesTimer.updateTime();
    
    // Check if we should retry the upload after connection is re-established
    if (shouldRetryUpload && wirelessManager->isWifiConnected()) {
        LOG_I(TAG, "Connection re-established, retrying upload");
        shouldRetryUpload = false;
        attemptUpload();
    }
    
    if(uploadMatchesTimer.expired()) {
        LOG_W(TAG, "Upload timeout expired");
        transitionToSleepState = true;
    }

    showLoadingGlyphs(pdn);
}

void UploadMatchesState::onStateDismounted(PDN* pdn) {
    LOG_I(TAG, "State dismounted");
    uploadMatchesTimer.invalidate();
    transitionToSleepState = false;
    shouldRetryUpload = false;
    matchUploadRetryCount = 0;
    // The SLEEP exit otherwise leaves WiFi up (scanning, if no AP in range) for
    // the whole sleep, jamming nearby ESP-NOW until the next Idle mount.
    pdn->getWirelessManager()->enablePeerCommsMode();
    pdn->getLightManager()->stopAnimation();
}

void UploadMatchesState::showLoadingGlyphs(PDN* pdn) {
    const int GLYPH_SIZE = 14;
    const int SCREEN_WIDTH = 128;
    const int SCREEN_HEIGHT = 64;
    
    const int GLYPHS_PER_ROW = (SCREEN_WIDTH / GLYPH_SIZE);
    const int GLYPHS_PER_COL = (SCREEN_HEIGHT - GLYPH_SIZE / GLYPH_SIZE);
    
    pdn->getDisplay()->invalidateScreen();
    pdn->getDisplay()->setGlyphMode(FontMode::LOADING_GLYPH);
    
    for (int row = 0; row < GLYPHS_PER_COL; row++) {
        for (int col = 0; col < GLYPHS_PER_ROW; col++) {
            if(rand() % 100 < 50) {
                int x = col * GLYPH_SIZE;
                int y = 14 + (row * GLYPH_SIZE);
                int randomIndex = rand() % 8;
                const char* glyph = loadingGlyphs[randomIndex];
                pdn->getDisplay()->renderGlyph(glyph, x, y);
            }
        }
    }
    
    pdn->getDisplay()->render();
}

bool UploadMatchesState::transitionToSleep() {
    return transitionToSleepState;
}   
