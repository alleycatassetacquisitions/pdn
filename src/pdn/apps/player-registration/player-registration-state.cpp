#include "apps/player-registration/player-registration-states.hpp"
#include "device/device.hpp"
#include "game/quickdraw-resources.hpp"
#include "device/animation/transmit-breath-animation.hpp"
#include "game/quickdraw-requests.hpp"
#include "device/drivers/logger.hpp"

static const char* TAG = "PlayerRegistrationState";

PlayerRegistrationState::PlayerRegistrationState(Player* player, MatchManager* matchManager) : TypedState<PDN>(PlayerRegistrationStateId::PLAYER_REGISTRATION) {
    LOG_I(TAG, "Initializing PlayerRegistrationState");
    this->player = player;
    this->matchManager = matchManager;
}

PlayerRegistrationState::~PlayerRegistrationState() {
    player = nullptr;
    LOG_I(TAG, "Destroying PlayerRegistrationState");
}

void PlayerRegistrationState::onStateMounted(PDN* pdn) {
    LOG_I(TAG, "State mounted - Starting player registration");

    pdn->getDisplay()->invalidateScreen()->
    setGlyphMode(FontMode::TEXT)->
    drawText("Pairing Code", 8, 16)->
    setGlyphMode(FontMode::NUMBER_GLYPH)->
    renderGlyph(digitGlyphs[0], 20, 40)->
    render();

    pdn->getPrimaryButton()->setButtonPress( 
    [](void *ctx) {
        PlayerRegistrationState* playerRegistration = (PlayerRegistrationState*)ctx;
        playerRegistration->currentDigit++;
        if(playerRegistration->currentDigit > 9) {
            playerRegistration->currentDigit = 0;
        }
        playerRegistration->shouldRender = true;
    }, this, ButtonInteraction::CLICK);

    pdn->getSecondaryButton()->setButtonPress( 
    [](void *ctx) {
        PlayerRegistrationState* playerRegistration = (PlayerRegistrationState*)ctx;
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
            playerRegistration->player->applyRngSeedFromUserId();
            playerRegistration->transitionToUserFetchState = true;
        } else {
            playerRegistration->shouldRender = true;
        }
    }, this, ButtonInteraction::CLICK);

    LOG_I(TAG, "Stored match count: %d", matchManager->getStoredMatchCount());
    if(matchManager->getStoredMatchCount() > 0) {
        LOG_I(TAG, "Starting transmit breath animation");
        AnimationConfig config;
        config.loop = true;
        config.speed = 25;
        config.initialState = LEDState();
        config.initialState.transmitLight = LEDState::SingleLEDState(LEDColor(bountyColors[0].red, bountyColors[0].green, bountyColors[0].blue), 255);
        pdn->getLightManager()->startAnimation(new TransmitBreathAnimation(), config);
    }
}

void PlayerRegistrationState::onStateLoop(PDN* pdn) {
    if(shouldRender) {
        if(currentDigitIndex == 0) {
            pdn->getDisplay()->invalidateScreen()->
            setGlyphMode(FontMode::TEXT)->
            drawText("Pairing Code", 8, 16)->
            setGlyphMode(FontMode::NUMBER_GLYPH)->
            renderGlyph(digitGlyphs[currentDigit], 20, 40)->
            render();
        } else if(currentDigitIndex == 1) {
            pdn->getDisplay()->invalidateScreen()->
            setGlyphMode(FontMode::TEXT)->
            drawText("Pairing Code", 8, 16)->
            setGlyphMode(FontMode::NUMBER_GLYPH)->
            renderGlyph(digitGlyphs[inputId[0]], 20, 40)->
            renderGlyph(digitGlyphs[currentDigit], 44, 40)->
            render();
        } else if(currentDigitIndex == 2) {
            pdn->getDisplay()->invalidateScreen()->
            setGlyphMode(FontMode::TEXT)->
            drawText("Pairing Code", 8, 16)->
            setGlyphMode(FontMode::NUMBER_GLYPH)->
            renderGlyph(digitGlyphs[inputId[0]], 20, 40)->
            renderGlyph(digitGlyphs[inputId[1]], 44, 40)->
            renderGlyph(digitGlyphs[currentDigit], 68, 40)->
            render();
        } else if(currentDigitIndex == 3) {
            pdn->getDisplay()->invalidateScreen()->
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

void PlayerRegistrationState::onStateDismounted(PDN* pdn) {
    LOG_I(TAG, "State dismounted - Cleaning up");
    pdn->getDisplay()->setGlyphMode(FontMode::TEXT);
    pdn->getPrimaryButton()->removeButtonCallbacks();
    pdn->getSecondaryButton()->removeButtonCallbacks();
    currentDigitIndex = 0;
    currentDigit = 0;
    transitionToUserFetchState = false;
    pdn->getLightManager()->stopAnimation();
}

bool PlayerRegistrationState::transitionToUserFetch() {
    return transitionToUserFetchState;
}
