#include "game/quickdraw-states.hpp"
#include "game/quickdraw-resources.hpp"
#include "game/quickdraw-requests.hpp"
#include "device/pdn.hpp"
#include "logger.hpp"

static const char* TAG = "PlayerRegistration";

PlayerRegistration::PlayerRegistration(Player* player, HttpClientInterface* httpClient, MatchManager* matchManager) : State(QuickdrawStateId::PLAYER_REGISTRATION) {
    LOG_I(TAG, "Initializing PlayerRegistration state");
    this->player = player;
    this->matchManager = matchManager;
    this->httpClient = httpClient;
}

PlayerRegistration::~PlayerRegistration() {
    player = nullptr;
    LOG_I(TAG, "Destroying PlayerRegistration state");
}

void PlayerRegistration::onStateMounted(Device *PDN) {
    LOG_I(TAG, "State mounted - Starting player registration");

    PDN->invalidateScreen()->
    setGlyphMode(FontMode::TEXT)->
    drawText("Pairing Code", 8, 16)->
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
        playerRegistration->shouldRender = true;
    }, this);

    PDN->setButtonClick(ButtonInteraction::CLICK, ButtonIdentifier::SECONDARY_BUTTON, 
    [](void *ctx) {
        PlayerRegistration* playerRegistration = (PlayerRegistration*)ctx;
        playerRegistration->inputId[playerRegistration->currentDigitIndex] = playerRegistration->currentDigit;

        playerRegistration->currentDigitIndex++;
        playerRegistration->currentDigit = 0;
        if(playerRegistration->currentDigitIndex >= playerRegistration->DIGIT_COUNT) {
            char playerId[5];
            snprintf(playerId, sizeof(playerId), "%d%d%d%d", 
                    playerRegistration->inputId[0],
                    playerRegistration->inputId[1],
                    playerRegistration->inputId[2],
                    playerRegistration->inputId[3]);
            LOG_I(TAG, "Player registration complete - ID: %s", playerId);
            playerRegistration->player->setUserID(playerId);
            playerRegistration->transitionToUserFetchState = true;
        } else {
            playerRegistration->shouldRender = true;
        }
    }, this);

    LOG_I(TAG, "Stored match count: %d", matchManager->getStoredMatchCount());
    if(matchManager->getStoredMatchCount() > 0) {
        LOG_I(TAG, "Starting transmit breath animation");
        AnimationConfig config;
        config.type = AnimationType::TRANSMIT_BREATH;
        config.loop = true;
        config.speed = 25;
        config.initialState = LEDState();
        config.initialState.transmitLight = LEDState::SingleLEDState(LEDColor(bountyColors[0].red, bountyColors[0].green, bountyColors[0].blue), 255);
        PDN->startAnimation(config);
    }
}

void PlayerRegistration::onStateLoop(Device *PDN) {
    if(shouldRender) {
        if(currentDigitIndex == 0) {
            PDN->invalidateScreen()->
            setGlyphMode(FontMode::TEXT)->
            drawText("Pairing Code", 8, 16)->
            setGlyphMode(FontMode::NUMBER_GLYPH)->
            renderGlyph(digitGlyphs[currentDigit], 20, 40)->
            render();
        } else if(currentDigitIndex == 1) {
            PDN->invalidateScreen()->
            setGlyphMode(FontMode::TEXT)->
            drawText("Pairing Code", 8, 16)->
            setGlyphMode(FontMode::NUMBER_GLYPH)->
            renderGlyph(digitGlyphs[inputId[0]], 20, 40)->
            renderGlyph(digitGlyphs[currentDigit], 44, 40)->
            render();
        } else if(currentDigitIndex == 2) {
            PDN->invalidateScreen()->
            setGlyphMode(FontMode::TEXT)->
            drawText("Pairing Code", 8, 16)->
            setGlyphMode(FontMode::NUMBER_GLYPH)->
            renderGlyph(digitGlyphs[inputId[0]], 20, 40)->
            renderGlyph(digitGlyphs[inputId[1]], 44, 40)->
            renderGlyph(digitGlyphs[currentDigit], 68, 40)->
            render();
        } else if(currentDigitIndex == 3) {
            PDN->invalidateScreen()->
            setGlyphMode(FontMode::TEXT)->
            drawText("Pairing Code", 8, 16)->
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

void PlayerRegistration::onStateDismounted(Device *PDN) {
    LOG_I(TAG, "State dismounted - Cleaning up");
    PDN->setGlyphMode(FontMode::TEXT);
    PDN->removeButtonCallbacks(ButtonIdentifier::PRIMARY_BUTTON);
    PDN->removeButtonCallbacks(ButtonIdentifier::SECONDARY_BUTTON);
    currentDigitIndex = 0;
    currentDigit = 0;
    transitionToUserFetchState = false;
    PDN->stopAnimation();
}

bool PlayerRegistration::transitionToUserFetch() {
    return transitionToUserFetchState;
}
