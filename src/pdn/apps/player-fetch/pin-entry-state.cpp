#include "apps/player-fetch/player-fetch-states.hpp"
#include "device/device.hpp"
#include "game/quickdraw-resources.hpp"
#include "device/drivers/logger.hpp"
#include "device/firmware-update-manager.hpp"

#include <cstdio>
#include <cstring>

static const char TAG[] = "PinEntryState";

PinEntryState::PinEntryState(Player* player)
    : TypedState<PDN>(PlayerFetchStateId::PIN_ENTRY)
    , player(player) {}

PinEntryState::~PinEntryState() { player = nullptr; }

void PinEntryState::onStateMounted(PDN* pdn) {
    sinceMount.setTimer(0);
    firmwareUpdateManager = pdn->getFirmwareUpdateManager();
    if (firmwareUpdateManager != nullptr) {
        firmwareUpdateManager->setEligible(true);
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
            pinEntry->player->setUserID(playerId);
            pinEntry->player->applyRngSeedFromUserId();
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
    if (shouldRender) {
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
    if (firmwareUpdateManager != nullptr) {
        firmwareUpdateManager->setEligible(false);
    }
}

bool PinEntryState::transitionToFetchUser() const { return transitionToFetchUserState; }

bool PinEntryState::transitionToChooseTeam() const { return transitionToChooseTeamState; }

bool PinEntryState::transitionToFirmwareUpdate() const { return transitionToFirmwareUpdateState; }
