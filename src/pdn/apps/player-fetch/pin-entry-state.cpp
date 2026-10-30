#include "apps/player-fetch/player-fetch-states.hpp"
#include "device/device.hpp"
#include "game/quickdraw-resources.hpp"
#include "device/drivers/logger.hpp"
#include "device/firmware-update-manager.hpp"

#include <cstdio>
#include <cstring>

static const char TAG[] = "PinEntryState";

PinEntryState::PinEntryState(PlayerProvider* playerProviderIn,
                             DeviceTestWirelessManager* deviceTestWirelessManagerIn)
    : TypedState<PDN>(PlayerFetchStateId::PIN_ENTRY)
    , deviceTestWirelessManager(deviceTestWirelessManagerIn)
    , playerProvider(playerProviderIn) {}

PinEntryState::~PinEntryState() {
    playerProvider = nullptr;
    deviceTestWirelessManager = nullptr;
}

void PinEntryState::onStateMounted(PDN* pdn) {
    playerProvider->clear();
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
            pinEntry->playerProvider->setRegistrationPin(playerId);
            pinEntry->playerProvider->applyRngSeedFromRegistrationPin();
            if (std::strcmp(playerId, DEMO_PIN) == 0) {
                pinEntry->transitionToDemoModeMenuState = true;
            } else {
                pinEntry->transitionToFetchUserState = true;
            }
        } else {
            pinEntry->shouldRender = true;
        }
    },
    this, ButtonInteraction::CLICK);

    if (deviceTestWirelessManager != nullptr) {
        deviceTestWirelessManager->registerEspNowHandler();
        deviceTestWirelessManager->setPacketReceivedCallback(
        [this](const uint8_t*, DeviceTestCommand command, const uint8_t*, size_t) {
            if (command == DeviceTestCommand::BEGIN_SESSION) {
                transitionToDeviceTestSubjectState = true;
            }
        });
    }
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
    transitionToDemoModeMenuState = false;
    transitionToFirmwareUpdateState = false;
    transitionToDeviceTestSubjectState = false;
    if (deviceTestWirelessManager != nullptr) {
        deviceTestWirelessManager->clearCallbacks();
        deviceTestWirelessManager->unregisterEspNowHandler();
    }
    if (firmwareUpdateManager != nullptr) {
        firmwareUpdateManager->setEligible(false);
    }
}

bool PinEntryState::transitionToFetchUser() const { return transitionToFetchUserState; }

bool PinEntryState::transitionToDemoModeMenu() const { return transitionToDemoModeMenuState; }

bool PinEntryState::transitionToFirmwareUpdate() const { return transitionToFirmwareUpdateState; }

bool PinEntryState::transitionToDeviceTestSubject() const {
    return transitionToDeviceTestSubjectState;
}
