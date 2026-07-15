#include "apps/bonk-it/bonk-it-states.hpp"
#include "device/drivers/logger.hpp"

namespace {
static const char* TAG = "ScoringState";

constexpr int kLeftButtonCenterX  = 32;
constexpr int kRightButtonCenterX = 96;
constexpr int kTitleY             = 12;
constexpr int kNameY              = 36;
constexpr int kButtonHintY        = 60;
}  // namespace

ScoringState::ScoringState(ControllerWirelessManager* controllerWirelessManager,
                           int* primaryScore,
                           const std::string* primaryLabel)
    : TypedState<FDN>(BonkItStateId::SCORING)
    , controllerWirelessManager_(controllerWirelessManager)
    , primaryScore_(primaryScore)
    , primaryLabel_(primaryLabel) {}

ScoringState::~ScoringState() {}

void ScoringState::onStateMounted(FDN* fdn) {
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
        auto* ss = static_cast<ScoringState*>(ctx);
        if (ss->phase_ != ScoringPhase::NAME_ENTRY) return;
        ss->currentColumn_ = (ss->currentColumn_ - 1 + kNumNameChars) % kNumNameChars;
    };
    parameterizedCallbackFunction onSecondary = [](void* ctx) {
        auto* ss = static_cast<ScoringState*>(ctx);
        if (ss->phase_ != ScoringPhase::NAME_ENTRY) return;
        ss->currentColumn_ = (ss->currentColumn_ + 1) % kNumNameChars;
    };
    parameterizedCallbackFunction onTertiary = [](void* ctx) {
        auto* ss = static_cast<ScoringState*>(ctx);
        if (ss->phase_ != ScoringPhase::NAME_ENTRY) return;
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
                phase_ = ScoringPhase::NAME_ENTRY;
                phaseTimer_.invalidate();
            }
            break;

        case ScoringPhase::NAME_ENTRY:
            renderScoringScreen(fdn);
            break;

        case ScoringPhase::THANKS:
            renderMessageScreen(fdn, "THANKS FOR", "BONKING!");
            if (phaseTimer_.expired()) {
                readyToTransition_ = true;
            }
            break;
    }
}

void ScoringState::onStateDismounted(FDN* fdn) {
    LOG_W(TAG, "Dismounted — name: %c%c%c score=%d",
          nameChars_[0], nameChars_[1], nameChars_[2],
          primaryScore_ ? *primaryScore_ : 0);

    fdn->getPrimaryButton()->removeButtonCallbacks();
    fdn->getSecondaryButton()->removeButtonCallbacks();
    fdn->getTertiaryButton()->removeButtonCallbacks();
    controllerWirelessManager_->clearCallback();
}

bool ScoringState::transitionToMainMenu() {
    return readyToTransition_;
}

void ScoringState::renderScoreIntroScreen(FDN* fdn) {
    const int score = primaryScore_ ? *primaryScore_ : 0;
    const std::string& scoreLabel = primaryLabel_ ? *primaryLabel_ : std::string("SCORE");
    std::string scoreLine = scoreLabel + ": " + std::to_string(score);

    fdn->getDisplay()
        ->invalidateScreen()
        ->setGlyphMode(FontMode::TEXT_INVERTED_LARGE)
        ->drawCenteredText(scoreLine.c_str(), 32)
        ->render();
}

void ScoringState::renderMessageScreen(FDN* fdn, const char* line1) {
    Display* d = fdn->getDisplay();
    d->invalidateScreen()->setGlyphMode(FontMode::TEXT_INVERTED_LARGE);
    d->drawCenteredText(line1, 32);
    d->render();
}

void ScoringState::renderMessageScreen(FDN* fdn, const char* line1, const char* line2) {
    Display* d = fdn->getDisplay();
    d->invalidateScreen()->setGlyphMode(FontMode::TEXT_INVERTED_LARGE);
    d->drawCenteredText(line1, 24);
    d->drawCenteredText(line2, 44);
    d->render();
}

void ScoringState::renderMessageScreen(FDN* fdn, const char* line1, const char* line2, const char* line3) {
    Display* d = fdn->getDisplay();
    d->invalidateScreen()->setGlyphMode(FontMode::TEXT_INVERTED_LARGE);
    d->drawCenteredText(line1, 16);
    d->drawCenteredText(line2, 32);
    d->drawCenteredText(line3, 48);
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

    if (command.buttonId == ButtonIdentifier::PRIMARY_BUTTON) {
        char& c = nameChars_[currentColumn_];
        c = (c <= kFirstChar) ? kLastChar : static_cast<char>(c - 1);
    } else if (command.buttonId == ButtonIdentifier::SECONDARY_BUTTON) {
        char& c = nameChars_[currentColumn_];
        c = (c >= kLastChar) ? kFirstChar : static_cast<char>(c + 1);
    }
}
