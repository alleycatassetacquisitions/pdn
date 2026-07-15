#include "apps/demo-modules/demo-module-states.hpp"
#include "device/drivers/logger.hpp"
#include "game/peripheral-glyphs.hpp"

namespace {
static const char* TAG = "ScoringState";

constexpr int kLeftButtonCenterX  = 32;
constexpr int kRightButtonCenterX = 96;
constexpr int kTitleY             = 12;
constexpr int kNameY              = 36;
constexpr int kButtonHintY        = 60;
}  // namespace

ScoringState::ScoringState(ControllerWirelessManager* controllerWirelessManager,
                             int* primaryScore, int* secondaryScore,
                             const std::string* primaryLabel, const std::string* secondaryLabel,
                             const std::string* thanksMessageLine2,
                             bool dualScoreDisplay,
                             bool showFarewellMessage)
    : TypedState<FDN>(DemoModuleStateId::SCORING)
    , controllerWirelessManager_(controllerWirelessManager)
    , primaryScore_(primaryScore)
    , secondaryScore_(secondaryScore)
    , primaryLabel_(primaryLabel)
    , secondaryLabel_(secondaryLabel)
    , thanksMessageLine2_(thanksMessageLine2)
    , dualScoreDisplay_(dualScoreDisplay)
    , showFarewellMessage_(showFarewellMessage) {}

ScoringState::~ScoringState() {}

void ScoringState::onStateMounted(FDN* fdn) {
    LOG_W(TAG, "Mounted");
    fdn_ = fdn;

    for (int i = 0; i < kNumNameChars; i++) {
        nameChars_[i] = kFirstChar;
    }
    currentColumn_     = 0;
    readyToTransition_ = false;
    phase_             = ScoringPhase::SHOW_SCORE;
    phaseTimer_.setTimer(kShowScoreDurationMs);

    renderScoreIntroScreen(fdn);

    parameterizedCallbackFunction onPrimary = [](void* ctx) {
        auto* ss = static_cast<ScoringState*>(ctx);
        if (ss->phase_ != ScoringPhase::NAME_ENTRY) return;
        ss->currentColumn_ = (ss->currentColumn_ - 1 + kNumNameChars) % kNumNameChars;
        ss->resetNameEntryInactivityTimer();
    };
    parameterizedCallbackFunction onSecondary = [](void* ctx) {
        auto* ss = static_cast<ScoringState*>(ctx);
        if (ss->phase_ != ScoringPhase::NAME_ENTRY) return;
        ss->currentColumn_ = (ss->currentColumn_ + 1) % kNumNameChars;
        ss->resetNameEntryInactivityTimer();
    };
    parameterizedCallbackFunction onTertiary = [](void* ctx) {
        auto* ss = static_cast<ScoringState*>(ctx);
        if (ss->phase_ != ScoringPhase::NAME_ENTRY) return;
        ss->endNameEntry(ss->fdn_);
        ss->phase_ = ScoringPhase::THANKS;
        ss->phaseTimer_.setTimer(kThanksDurationMs);
    };

    fdn->getPrimaryButton()->setButtonPress(onPrimary, this, ButtonInteraction::PRESS);
    fdn->getSecondaryButton()->setButtonPress(onSecondary, this, ButtonInteraction::PRESS);
    fdn->getTertiaryButton()->setButtonPress(onTertiary, this, ButtonInteraction::PRESS);

    controllerWirelessManager_->setControllerCommandReceivedCallback(
        std::bind(&ScoringState::onControllerCommandReceived, this, std::placeholders::_1),
        SerialIdentifier::INPUT_JACK);
    controllerWirelessManager_->setControllerCommandReceivedCallback(
        std::bind(&ScoringState::onControllerCommandReceived, this, std::placeholders::_1),
        SerialIdentifier::INPUT_JACK_SECONDARY);
}

void ScoringState::onStateLoop(FDN* fdn) {
    switch (phase_) {
        case ScoringPhase::SHOW_SCORE:
            renderScoreIntroScreen(fdn);
            if (phaseTimer_.expired()) {
                beginNameEntry(fdn);
            }
            break;

        case ScoringPhase::NAME_ENTRY:
            renderScoringScreen(fdn);
            if (inactivityTimer_.expired()) {
                LOG_W(TAG, "Name entry timed out — returning to main menu");
                endNameEntry(fdn);
                readyToTransition_ = true;
            }
            break;

        case ScoringPhase::THANKS: {
            const char* thanksLine2 = thanksMessageLine2_ ? thanksMessageLine2_->c_str() : "PLAYING!";
            renderMessageScreen(fdn, "THANKS FOR", thanksLine2);
            if (phaseTimer_.expired()) {
                if (showFarewellMessage_) {
                    phase_ = ScoringPhase::FAREWELL;
                    phaseTimer_.setTimer(kFarewellDurationMs);
                } else {
                    readyToTransition_ = true;
                }
            }
            break;
        }

        case ScoringPhase::FAREWELL:
            renderMessageScreen(fdn, "MAY FORTUNE", "FAVOR YOU,", "COWBOY");
            if (phaseTimer_.expired()) {
                readyToTransition_ = true;
            }
            break;
    }
}

void ScoringState::onStateDismounted(FDN* fdn) {
    LOG_W(TAG, "Dismounted — name: %c%c%c  primary=%d secondary=%d",
          nameChars_[0], nameChars_[1], nameChars_[2],
          primaryScore_ ? *primaryScore_ : 0,
          secondaryScore_ ? *secondaryScore_ : 0);

    endNameEntry(fdn);
    fdn_ = nullptr;

    fdn->getPrimaryButton()->removeButtonCallbacks();
    fdn->getSecondaryButton()->removeButtonCallbacks();
    fdn->getTertiaryButton()->removeButtonCallbacks();
    controllerWirelessManager_->clearCallback();
}

bool ScoringState::transitionToMainMenu() {
    return readyToTransition_;
}

void ScoringState::beginNameEntry(FDN* fdn) {
    (void)fdn;
    phase_ = ScoringPhase::NAME_ENTRY;
    phaseTimer_.invalidate();
    resetNameEntryInactivityTimer();
    controllerWirelessManager_->sendPeripheralCommandPacket(
        PeripheralCmd::DISPLAY_GLYPH,
        static_cast<uint8_t>(PeripheralGlyphId::SCORING),
        0);
}

void ScoringState::endNameEntry(FDN* fdn) {
    (void)fdn;
    inactivityTimer_.invalidate();
    controllerWirelessManager_->sendPeripheralCommandPacket(
        PeripheralCmd::CLEAR_DISPLAY, 0, 0);
}

void ScoringState::resetNameEntryInactivityTimer() {
    if (phase_ == ScoringPhase::NAME_ENTRY) {
        inactivityTimer_.setTimer(kNameEntryInactivityTimeoutMs);
    }
}

void ScoringState::renderScoreIntroScreen(FDN* fdn) {
    const int primary = primaryScore_ ? *primaryScore_ : 0;
    Display* d = fdn->getDisplay();
    d->invalidateScreen()->setGlyphMode(FontMode::TEXT_INVERTED_LARGE);

    if (dualScoreDisplay_) {
        const int secondary = secondaryScore_ ? *secondaryScore_ : 0;
        const std::string& primaryLabel =
            primaryLabel_ ? *primaryLabel_ : std::string("SCORE");
        const std::string& secondaryLabel =
            secondaryLabel_ ? *secondaryLabel_ : std::string("AVG MS");
        std::string line1 = primaryLabel + ": " + std::to_string(primary);
        std::string line2 = secondaryLabel + ": " + std::to_string(secondary);
        d->drawCenteredText(line1.c_str(), 24);
        d->drawCenteredText(line2.c_str(), 44);
    } else {
        const std::string& scoreLabel =
            primaryLabel_ ? *primaryLabel_ : std::string("SCORE");
        std::string scoreLine = scoreLabel + ": " + std::to_string(primary);
        d->drawCenteredText(scoreLine.c_str(), 32);
    }

    d->render();
}

void ScoringState::renderMessageScreen(FDN* fdn, const char* line1, const char* line2, const char* line3) {
    Display* d = fdn->getDisplay();
    d->invalidateScreen()->setGlyphMode(FontMode::TEXT_INVERTED_LARGE);
    if (line3) {
        d->drawCenteredText(line1, 16);
        d->drawCenteredText(line2, 32);
        d->drawCenteredText(line3, 48);
    } else if (line2) {
        d->drawCenteredText(line1, 24);
        d->drawCenteredText(line2, 44);
    } else {
        d->drawCenteredText(line1, 32);
    }
    d->render();
}

void ScoringState::renderScoringScreen(FDN* fdn) {
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

void ScoringState::onControllerCommandReceived(ControllerCommand command) {
    if (command.command != ControllerCmd::INTERACTION_REQUEST) return;
    if (phase_ != ScoringPhase::NAME_ENTRY) return;
    if (command.interactionId != ButtonInteraction::PRESS) return;

    resetNameEntryInactivityTimer();

    if (command.buttonId == ButtonIdentifier::PRIMARY_BUTTON) {
        char& c = nameChars_[currentColumn_];
        c = (c <= kFirstChar) ? kLastChar : static_cast<char>(c - 1);
        LOG_W(TAG, "Letter[%d] back: %c", currentColumn_, c);
    } else if (command.buttonId == ButtonIdentifier::SECONDARY_BUTTON) {
        char& c = nameChars_[currentColumn_];
        c = (c >= kLastChar) ? kFirstChar : static_cast<char>(c + 1);
        LOG_W(TAG, "Letter[%d] forward: %c", currentColumn_, c);
    }
}
