#include "apps/player-fetch/player-fetch-states.hpp"
#include "device/device.hpp"
#include "game/quickdraw-resources.hpp"
#include "device/drivers/logger.hpp"
#include "device/firmware-update-manager.hpp"
#include "device/animation/blink-animation.hpp"
#include "device/animation/transmit-blink-animation.hpp"

#include <cstdio>
#include <cstring>

static const char TAG[] = "PinEntryState";

namespace {

constexpr LEDColor UPDATED_COLOR = LEDColor(0, 255, 0);
constexpr LEDColor RECEIVING_COLOR = LEDColor(0, 0, 255);
constexpr LEDColor UPDATE_FAILED_COLOR = LEDColor(255, 0, 0);
constexpr uint8_t UPDATE_LIGHT_BRIGHTNESS = 255;
constexpr unsigned long UPDATED_BLINK_MS = 500;
constexpr unsigned long RECEIVING_BLINK_MS = 150;
constexpr unsigned long UPDATE_FAILED_BLINK_MS = 250;
constexpr uint8_t UPDATE_FAILED_BLINKS =
static_cast<uint8_t>(PLAYER_FETCH_UPDATE_FAILED_MS / (2 * UPDATE_FAILED_BLINK_MS));
constexpr uint8_t UPDATE_LIGHT_FRAME_MS = 16;

}  // namespace

PinEntryState::PinEntryState(PlayerProvider* playerProvider)
    : TypedState<PDN>(PlayerFetchStateId::PIN_ENTRY)
    , playerProvider(playerProvider) {}

PinEntryState::~PinEntryState() { playerProvider = nullptr; }

void PinEntryState::onStateMounted(PDN* pdn) {
    playerProvider->clear();
    sinceMount.setTimer(0);
    firmwareUpdateManager = pdn->getFirmwareUpdateManager();
    updateView = UpdateView::PAIRING;
    if (firmwareUpdateManager != nullptr) {
        firmwareUpdateManager->setEligible(true);
        showPairingLights(pdn);
    }

    pdn->getDisplay()
    ->invalidateScreen()
    ->setGlyphMode(FontMode::TEXT)
    ->drawText("Pairing Code", 8, 16)
    ->setGlyphMode(FontMode::NUMBER_GLYPH)
    ->renderGlyph(digitGlyphs[0], 20, 40)
    ->render();

    pdn->getPrimaryButton()->setButtonPress(
    [](void* ctx) {
        PinEntryState* pinEntry = static_cast<PinEntryState*>(ctx);
        pinEntry->currentDigit++;
        if (pinEntry->currentDigit > 9) {
            pinEntry->currentDigit = 0;
        }
        pinEntry->shouldRender = true;
    },
    this, ButtonInteraction::CLICK);

    pdn->getSecondaryButton()->setButtonPress(
    [](void* ctx) {
        PinEntryState* pinEntry = static_cast<PinEntryState*>(ctx);
        pinEntry->inputId[pinEntry->currentDigitIndex] = pinEntry->currentDigit;

        pinEntry->currentDigitIndex++;
        pinEntry->currentDigit = 0;
        if (pinEntry->currentDigitIndex >= pinEntry->DIGIT_COUNT) {
            char playerId[5];
            snprintf(playerId, sizeof(playerId), "%d%d%d%d", pinEntry->inputId[0],
                     pinEntry->inputId[1], pinEntry->inputId[2], pinEntry->inputId[3]);
            LOG_I(TAG, "Pairing complete - PIN: %s", playerId);
            pinEntry->playerProvider->setRegistrationPin(playerId);
            pinEntry->playerProvider->applyRngSeedFromRegistrationPin();
            if (std::strcmp(playerId, DEMO_PIN) == 0) {
                pinEntry->transitionToChooseTeamState = true;
            } else {
                pinEntry->transitionToFetchUserState = true;
            }
        } else {
            pinEntry->shouldRender = true;
        }
    },
    this, ButtonInteraction::CLICK);
}

void PinEntryState::onStateLoop(PDN* pdn) {
    const unsigned long heldMs = pdn->getSecondaryButton()->longPressedMillis();
    if (heldMs >= PLAYER_FETCH_FIRMWARE_HOLD_MS && heldMs <= sinceMount.getElapsedTime()) {
        transitionToFirmwareUpdateState = true;
    }
    if (firmwareUpdateManager != nullptr) {
        syncUpdateFeedback(pdn);
    }
    // Digits pressed while an update is on screen show once the pairing screen returns.
    if (shouldRender && updateView == UpdateView::PAIRING) {
        if (currentDigitIndex == 0) {
            pdn->getDisplay()
            ->invalidateScreen()
            ->setGlyphMode(FontMode::TEXT)
            ->drawText("Pairing Code", 8, 16)
            ->setGlyphMode(FontMode::NUMBER_GLYPH)
            ->renderGlyph(digitGlyphs[currentDigit], 20, 40)
            ->render();
        } else if (currentDigitIndex == 1) {
            pdn->getDisplay()
            ->invalidateScreen()
            ->setGlyphMode(FontMode::TEXT)
            ->drawText("Pairing Code", 8, 16)
            ->setGlyphMode(FontMode::NUMBER_GLYPH)
            ->renderGlyph(digitGlyphs[inputId[0]], 20, 40)
            ->renderGlyph(digitGlyphs[currentDigit], 44, 40)
            ->render();
        } else if (currentDigitIndex == 2) {
            pdn->getDisplay()
            ->invalidateScreen()
            ->setGlyphMode(FontMode::TEXT)
            ->drawText("Pairing Code", 8, 16)
            ->setGlyphMode(FontMode::NUMBER_GLYPH)
            ->renderGlyph(digitGlyphs[inputId[0]], 20, 40)
            ->renderGlyph(digitGlyphs[inputId[1]], 44, 40)
            ->renderGlyph(digitGlyphs[currentDigit], 68, 40)
            ->render();
        } else if (currentDigitIndex == 3) {
            pdn->getDisplay()
            ->invalidateScreen()
            ->setGlyphMode(FontMode::TEXT)
            ->drawText("Pairing Code", 8, 16)
            ->setGlyphMode(FontMode::NUMBER_GLYPH)
            ->renderGlyph(digitGlyphs[inputId[0]], 20, 40)
            ->renderGlyph(digitGlyphs[inputId[1]], 44, 40)
            ->renderGlyph(digitGlyphs[inputId[2]], 68, 40)
            ->renderGlyph(digitGlyphs[currentDigit], 92, 40)
            ->render();
        }
        shouldRender = false;
    }
}

void PinEntryState::onStateDismounted(PDN* pdn) {
    pdn->getDisplay()->setGlyphMode(FontMode::TEXT);
    pdn->getPrimaryButton()->removeButtonCallbacks();
    pdn->getSecondaryButton()->removeButtonCallbacks();
    currentDigitIndex = 0;
    currentDigit = 0;
    transitionToFetchUserState = false;
    transitionToChooseTeamState = false;
    transitionToFirmwareUpdateState = false;
    updateView = UpdateView::PAIRING;
    if (firmwareUpdateManager != nullptr) {
        firmwareUpdateManager->setEligible(false);
        firmwareUpdateManager->acknowledgeUpdate();
    }
    if (ownsLights) {
        pdn->getLightManager()->stopAnimation();
        ownsLights = false;
    }
}

void PinEntryState::syncUpdateFeedback(PDN* pdn) {
    if (firmwareUpdateManager->takeReceiveFailure()) {
        showUpdateFailed(pdn);
        return;
    }
    if (updateView == UpdateView::FAILED) {
        if (updateFailedTimer.expired()) {
            showPairing(pdn);
        }
        return;
    }
    if (firmwareUpdateManager->isReceiving()) {
        const uint8_t percent = firmwareUpdateManager->receivePercent();
        if (updateView != UpdateView::RECEIVING || percent != shownPercent) {
            showReceiving(pdn, percent);
        }
    } else if (updateView == UpdateView::RECEIVING) {
        showPairing(pdn);
    }
}

void PinEntryState::showPairing(PDN* pdn) {
    updateView = UpdateView::PAIRING;
    shouldRender = true;
    showPairingLights(pdn);
}

void PinEntryState::showPairingLights(PDN* pdn) {
    if (firmwareUpdateManager->isFreshlyUpdated()) {
        startLights(pdn, new TransmitBlinkAnimation(UPDATED_COLOR, UPDATE_LIGHT_BRIGHTNESS,
                                                    UPDATED_BLINK_MS, UPDATED_BLINK_MS));
    } else if (ownsLights) {
        pdn->getLightManager()->stopAnimation();
        ownsLights = false;
    }
}

void PinEntryState::showReceiving(PDN* pdn, uint8_t percent) {
    if (updateView != UpdateView::RECEIVING) {
        updateView = UpdateView::RECEIVING;
        startLights(pdn, new TransmitBlinkAnimation(RECEIVING_COLOR, UPDATE_LIGHT_BRIGHTNESS,
                                                    RECEIVING_BLINK_MS, RECEIVING_BLINK_MS));
    }
    shownPercent = percent;
    char progress[16];
    snprintf(progress, sizeof(progress), "Receiving %u%%", static_cast<unsigned>(percent));
    Display* d = pdn->getDisplay();
    d->invalidateScreen()->setGlyphMode(FontMode::TEXT_INVERTED_LARGE);
    d->drawCenteredText("UPDATING", 25);
    d->setGlyphMode(FontMode::TEXT_INVERTED_SMALL);
    d->drawCenteredText(progress, 50);
    d->render();
}

void PinEntryState::showUpdateFailed(PDN* pdn) {
    LOG_W(TAG, "Firmware update failed; keeping the running build");
    updateView = UpdateView::FAILED;
    updateFailedTimer.setTimer(PLAYER_FETCH_UPDATE_FAILED_MS);
    startLights(pdn, new BlinkAnimation(UPDATE_FAILED_COLOR, UPDATE_LIGHT_BRIGHTNESS,
                                        UPDATE_FAILED_BLINK_MS, UPDATE_FAILED_BLINK_MS,
                                        UPDATE_FAILED_BLINKS));
    Display* d = pdn->getDisplay();
    d->invalidateScreen()->setGlyphMode(FontMode::TEXT_INVERTED_LARGE);
    d->drawCenteredText("FAILED", 25);
    d->setGlyphMode(FontMode::TEXT_INVERTED_SMALL);
    d->drawCenteredText("Old build kept", 50);
    d->render();
}

void PinEntryState::startLights(PDN* pdn, AnimationBase* animation) {
    AnimationConfig config;
    config.speed = UPDATE_LIGHT_FRAME_MS;
    config.loop = true;
    pdn->getLightManager()->startAnimation(animation, config);
    ownsLights = true;
}

bool PinEntryState::transitionToFetchUser() const { return transitionToFetchUserState; }

bool PinEntryState::transitionToChooseTeam() const { return transitionToChooseTeamState; }

bool PinEntryState::transitionToFirmwareUpdate() const { return transitionToFirmwareUpdateState; }
