#include "apps/crypt-creeper/crypt-creeper-states.hpp"
#include "device/drivers/button.hpp"
#include "device/drivers/logger.hpp"
#include "device/drivers/peer-comms-types.hpp"
#include "game/peripheral-glyphs.hpp"
#include <cstdio>

namespace {
static const char* TAG = "CryptCreeperScoringState";

constexpr int kLeftButtonCenterX  = 32;
constexpr int kRightButtonCenterX = 96;
constexpr int kTitleY             = 12;
constexpr int kNameY              = 36;
constexpr int kButtonHintY        = 60;
}  // namespace

CryptCreeperScoringState::CryptCreeperScoringState(ControllerWirelessManager* controllerWirelessManager,
                           unsigned long* elapsedMs)
    : TypedState<FDN>(CryptCreeperStateId::SCORING)
    , controllerWirelessManager_(controllerWirelessManager)
    , elapsedMs_(elapsedMs) {}

CryptCreeperScoringState::~CryptCreeperScoringState() {}

void CryptCreeperScoringState::onStateMounted(FDN* fdn) {
    LOG_W(TAG, "Mounted");

    for (int i = 0; i < kNumNameChars; i++) {
        nameChars_[i] = kFirstChar;
    }
    currentColumn_     = 0;
    readyToTransition_ = false;
    phase_             = ScoringPhase::SHOW_SCORE;
    phaseTimer_.setTimer(kShowScoreDurationMs);

    renderScoreIntroScreen(fdn);

    parameterizedCallbackFunction onPrimary = [](void* ctx) {
        auto* ss = static_cast<CryptCreeperScoringState*>(ctx);
        if (ss->phase_ != ScoringPhase::NAME_ENTRY) return;
        ss->currentColumn_ = (ss->currentColumn_ - 1 + kNumNameChars) % kNumNameChars;
    };
    parameterizedCallbackFunction onSecondary = [](void* ctx) {
        auto* ss = static_cast<CryptCreeperScoringState*>(ctx);
        if (ss->phase_ != ScoringPhase::NAME_ENTRY) return;
        ss->currentColumn_ = (ss->currentColumn_ + 1) % kNumNameChars;
    };
    parameterizedCallbackFunction onTertiary = [](void* ctx) {
        auto* ss = static_cast<CryptCreeperScoringState*>(ctx);
        if (ss->phase_ != ScoringPhase::NAME_ENTRY) return;
        ss->controllerWirelessManager_->sendPeripheralCommandPacket(
            PeripheralCmd::CLEAR_DISPLAY, 0, 0);
        ss->phase_ = ScoringPhase::THANKS;
        ss->phaseTimer_.setTimer(kThanksDurationMs);
    };

    fdn->getPrimaryButton()->setButtonPress(onPrimary, this, ButtonInteraction::PRESS);
    fdn->getSecondaryButton()->setButtonPress(onSecondary, this, ButtonInteraction::PRESS);
    fdn->getTertiaryButton()->setButtonPress(onTertiary, this, ButtonInteraction::PRESS);

    controllerWirelessManager_->setControllerCommandReceivedCallback(
        std::bind(&CryptCreeperScoringState::onControllerCommandReceived, this, std::placeholders::_1),
        SerialIdentifier::INPUT_JACK);
    controllerWirelessManager_->setControllerCommandReceivedCallback(
        std::bind(&CryptCreeperScoringState::onControllerCommandReceived, this, std::placeholders::_1),
        SerialIdentifier::INPUT_JACK_SECONDARY);
}

void CryptCreeperScoringState::onStateLoop(FDN* fdn) {
    switch (phase_) {
        case ScoringPhase::SHOW_SCORE:
            renderScoreIntroScreen(fdn);
            if (phaseTimer_.expired()) {
                phase_ = ScoringPhase::NAME_ENTRY;
                phaseTimer_.invalidate();
                controllerWirelessManager_->sendPeripheralCommandPacket(
                    PeripheralCmd::DISPLAY_GLYPH,
                    static_cast<uint8_t>(PeripheralGlyphId::SCORING),
                    0);
            }
            break;

        case ScoringPhase::NAME_ENTRY:
            renderScoringScreen(fdn);
            break;

        case ScoringPhase::THANKS:
            renderMessageScreen(fdn, "THANKS FOR", "CREEPIN'!");
            if (phaseTimer_.expired()) {
                readyToTransition_ = true;
            }
            break;
    }
}

void CryptCreeperScoringState::onStateDismounted(FDN* fdn) {
    LOG_W(TAG, "Dismounted — name: %c%c%c elapsed=%lums",
          nameChars_[0], nameChars_[1], nameChars_[2],
          elapsedMs_ ? *elapsedMs_ : 0UL);

    phaseTimer_.invalidate();
    readyToTransition_ = false;
    fdn->getPrimaryButton()->removeButtonCallbacks();
    fdn->getSecondaryButton()->removeButtonCallbacks();
    fdn->getTertiaryButton()->removeButtonCallbacks();
    controllerWirelessManager_->clearCallback();
    controllerWirelessManager_->sendPeripheralCommandPacket(
        PeripheralCmd::CLEAR_DISPLAY, 0, 0);
}

bool CryptCreeperScoringState::transitionToMainMenu() {
    return readyToTransition_;
}

void CryptCreeperScoringState::renderScoreIntroScreen(FDN* fdn) {
    const unsigned long elapsed = elapsedMs_ ? *elapsedMs_ : 0UL;
    const unsigned long seconds = elapsed / 1000UL;
    const unsigned long tenths  = (elapsed % 1000UL) / 100UL;

    char timeLine[24];
    snprintf(timeLine, sizeof(timeLine), "TIME: %lu.%lus", seconds, tenths);

    fdn->getDisplay()
        ->invalidateScreen()
        ->setGlyphMode(FontMode::TEXT_INVERTED_LARGE)
        ->drawCenteredText("COMPLETE!", 20)
        ->setGlyphMode(FontMode::TEXT_INVERTED_SMALL)
        ->drawCenteredText(timeLine, 44)
        ->render();
}

void CryptCreeperScoringState::renderMessageScreen(FDN* fdn, const char* line1, const char* line2) {
    fdn->getDisplay()
        ->invalidateScreen()
        ->setGlyphMode(FontMode::TEXT_INVERTED_LARGE)
        ->drawCenteredText(line1, 24)
        ->drawCenteredText(line2, 44)
        ->render();
}

void CryptCreeperScoringState::renderScoringScreen(FDN* fdn) {
    std::string nameLine;
    for (int i = 0; i < kNumNameChars; i++) {
        if (i == currentColumn_) {
            nameLine += '[';
            nameLine += nameChars_[i];
            nameLine += ']';
        } else {
            nameLine += ' ';
            nameLine += nameChars_[i];
            nameLine += ' ';
        }
        if (i < kNumNameChars - 1) {
            nameLine += ' ';
        }
    }

    Display* d = fdn->getDisplay();
    d->invalidateScreen()
        ->setGlyphMode(FontMode::TEXT_INVERTED_SMALL)
        ->drawCenteredText("ENTER NAME", kTitleY)
        ->setGlyphMode(FontMode::TEXT_INVERTED_LARGE)
        ->drawCenteredText(nameLine.c_str(), kNameY)
        ->setGlyphMode(FontMode::TEXT_INVERTED_SMALL)
        ->drawText("<", centeredTextXInHalf(d, "<", kLeftButtonCenterX), kButtonHintY)
        ->drawText(">", centeredTextXInHalf(d, ">", kRightButtonCenterX), kButtonHintY)
        ->render();
}

void CryptCreeperScoringState::onControllerCommandReceived(ControllerCommand command) {
    if (command.command != ControllerCmd::INTERACTION_REQUEST) {
        return;
    }
    if (phase_ != ScoringPhase::NAME_ENTRY) {
        return;
    }
    if (command.interactionId != ButtonInteraction::PRESS) {
        return;
    }

    if (command.buttonId == ButtonIdentifier::PRIMARY_BUTTON) {
        char& c = nameChars_[currentColumn_];
        c = (c <= kFirstChar) ? kLastChar : static_cast<char>(c - 1);
    } else if (command.buttonId == ButtonIdentifier::SECONDARY_BUTTON) {
        char& c = nameChars_[currentColumn_];
        c = (c >= kLastChar) ? kFirstChar : static_cast<char>(c + 1);
    }
}
