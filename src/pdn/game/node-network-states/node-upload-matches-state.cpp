#include "game/node-network-states.hpp"
#include "game/node-network-resources.hpp"
#include "game/quickdraw-requests.hpp"
#include "device/animation/transmit-breath-animation.hpp"
#include "device/device.hpp"
#include "device/drivers/logger.hpp"

static const char* TAG = "NodeUploadMatches";

NodeUploadMatches::NodeUploadMatches(Player* player, WirelessManager* wirelessManager, NodeMatchManager* matchManager)
    : TypedState<PDN>(NodeNetworkStateId::UPLOAD_MATCHES) {
    this->player = player;
    this->wirelessManager = wirelessManager;
    this->matchManager = matchManager;
}

NodeUploadMatches::~NodeUploadMatches() {
    player = nullptr;
    wirelessManager = nullptr;
    matchManager = nullptr;
}

void NodeUploadMatches::attemptUpload() {
    QuickdrawRequests::updateMatches(
        wirelessManager,
        matchesJson,
        [this](const std::string& jsonResponse) {
            LOG_I(TAG, "Successfully updated matches: %s", jsonResponse.c_str());
            matchManager->clearStorage();
            transitionToSleepState = true;
        },
        [this](const WirelessErrorInfo& error) {
            LOG_E(TAG, "Failed to update matches: %s (code: %d)", error.message.c_str(), static_cast<int>(error.code));
            if (matchUploadRetryCount < 3) {
                matchUploadRetryCount++;
                uploadMatchesTimer.setTimer(UPLOAD_MATCHES_TIMEOUT);
                shouldRetryUpload = true;
            } else {
                transitionToSleepState = true;
            }
        });
}

void NodeUploadMatches::onStateMounted(PDN* pdn) {
    pdn->getWirelessManager()->enableWifiMode();
    showLoadingGlyphs(pdn);
    uploadMatchesTimer.setTimer(UPLOAD_MATCHES_TIMEOUT);
    matchUploadRetryCount = 0;
    matchesJson = matchManager->toJson();
    attemptUpload();

    AnimationConfig config;
    config.loop = true;
    config.speed = 10;
    config.initialState = LEDState();
    config.initialState.transmitLight = LEDState::SingleLEDState(
        LEDColor(bountyColors[0].red, bountyColors[0].green, bountyColors[0].blue), 255);
    pdn->getLightManager()->startAnimation(new TransmitBreathAnimation(), config);
}

void NodeUploadMatches::onStateLoop(PDN* pdn) {
    uploadMatchesTimer.updateTime();
    if (shouldRetryUpload && wirelessManager->isWifiConnected()) {
        shouldRetryUpload = false;
        attemptUpload();
    }
    if (uploadMatchesTimer.expired()) {
        transitionToSleepState = true;
    }
    showLoadingGlyphs(pdn);
}

void NodeUploadMatches::onStateDismounted(PDN* pdn) {
    uploadMatchesTimer.invalidate();
    transitionToSleepState = false;
    shouldRetryUpload = false;
    matchUploadRetryCount = 0;
    pdn->getLightManager()->stopAnimation();
}

void NodeUploadMatches::showLoadingGlyphs(PDN* pdn) {
    const int GLYPH_SIZE = 14;
    const int SCREEN_WIDTH = 128;
    const int SCREEN_HEIGHT = 64;
    const int GLYPHS_PER_ROW = (SCREEN_WIDTH / GLYPH_SIZE);
    const int GLYPHS_PER_COL = (SCREEN_HEIGHT - GLYPH_SIZE / GLYPH_SIZE);

    pdn->getDisplay()->invalidateScreen();
    pdn->getDisplay()->setGlyphMode(FontMode::LOADING_GLYPH);
    for (int row = 0; row < GLYPHS_PER_COL; row++) {
        for (int col = 0; col < GLYPHS_PER_ROW; col++) {
            if (rand() % 100 < 50) {
                int x = col * GLYPH_SIZE;
                int y = 14 + (row * GLYPH_SIZE);
                pdn->getDisplay()->renderGlyph(loadingGlyphs[rand() % 8], x, y);
            }
        }
    }
    pdn->getDisplay()->render();
}

bool NodeUploadMatches::transitionToSleep() {
    return transitionToSleepState;
}
