#include "apps/demo-modules/demo-module-states.hpp"
#include "device/drivers/logger.hpp"

namespace {
static const char* TAG = "ScoringState";
}

ScoringState::ScoringState(ControllerWirelessManager* controllerWirelessManager,
                             int* primaryScore, int* secondaryScore,
                             const std::string* primaryLabel, const std::string* secondaryLabel)
    : TypedState<FDN>(DemoModuleStateId::SCORING)
    , controllerWirelessManager_(controllerWirelessManager)
    , primaryScore_(primaryScore)
    , secondaryScore_(secondaryScore)
    , primaryLabel_(primaryLabel)
    , secondaryLabel_(secondaryLabel) {}

ScoringState::~ScoringState() {}

void ScoringState::onStateMounted(FDN* fdn) {
    LOG_W(TAG, "Mounted");

    // Reset name entry.
    for (int i = 0; i < kNumNameChars; i++) nameChars_[i] = kFirstChar;
    currentColumn_    = 0;
    readyToTransition_ = false;
    phase_            = ScoringPhase::SHOW_SCORE;
    phaseTimer_.setTimer(kShowScoreDurationMs);

    renderScoreIntroScreen(fdn);

    // FDN primary button: move column left (only active during NAME_ENTRY).
    parameterizedCallbackFunction onPrimary = [](void* ctx) {
        auto* ss = static_cast<ScoringState*>(ctx);
        if (ss->phase_ != ScoringPhase::NAME_ENTRY) return;
        ss->currentColumn_ = (ss->currentColumn_ - 1 + kNumNameChars) % kNumNameChars;
    };
    // FDN secondary button: move column right (only active during NAME_ENTRY).
    parameterizedCallbackFunction onSecondary = [](void* ctx) {
        auto* ss = static_cast<ScoringState*>(ctx);
        if (ss->phase_ != ScoringPhase::NAME_ENTRY) return;
        ss->currentColumn_ = (ss->currentColumn_ + 1) % kNumNameChars;
    };
    // FDN tertiary button: submit name and begin outro sequence.
    parameterizedCallbackFunction onTertiary = [](void* ctx) {
        auto* ss = static_cast<ScoringState*>(ctx);
        if (ss->phase_ != ScoringPhase::NAME_ENTRY) return;
        ss->phase_ = ScoringPhase::THANKS;
        ss->phaseTimer_.setTimer(kThanksDurationMs);
    };

    fdn->getPrimaryButton()->setButtonPress(onPrimary,    this, ButtonInteraction::PRESS);
    fdn->getSecondaryButton()->setButtonPress(onSecondary, this, ButtonInteraction::PRESS);
    fdn->getTertiaryButton()->setButtonPress(onTertiary,  this, ButtonInteraction::PRESS);

    // PDN controller commands: top button cycles letter up, bottom cycles down.
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
            renderMessageScreen(fdn, "THANKS FOR", "PLAYING!");
            if (phaseTimer_.expired()) {
                phase_ = ScoringPhase::FAREWELL;
                phaseTimer_.setTimer(kFarewellDurationMs);
            }
            break;

        case ScoringPhase::FAREWELL:
            renderMessageScreen(fdn, "MAY FORTUNE", "FAVOR YOU,", "COWBOY");
            if (phaseTimer_.expired()) {
                readyToTransition_ = true;
            }
            break;
    }
}

void ScoringState::onStateDismounted(FDN* fdn) {
    LOG_W(TAG, "Dismounted — name: %c%c%c  score=%d",
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
    int score = primaryScore_ ? *primaryScore_ : 0;
    std::string scoreLine = "SCORE: " + std::to_string(score);

    fdn->getDisplay()
        ->invalidateScreen()
        ->setGlyphMode(FontMode::TEXT_INVERTED_LARGE)
        ->drawCenteredText(scoreLine.c_str(), 32)
        ->render();
}

void ScoringState::renderMessageScreen(FDN* fdn, const char* line1, const char* line2, const char* line3) {
    Display* d = fdn->getDisplay();
    d->invalidateScreen()->setGlyphMode(FontMode::TEXT_INVERTED_LARGE);
    if (line3) {
        // Three lines: spread across the display.
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
    // Build the name line: inactive letters are " X ", active is "[X]".
    // Rendered large and centred on screen.
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
        if (i < kNumNameChars - 1) nameLine += ' ';
    }

    fdn->getDisplay()
        ->invalidateScreen()
        ->setGlyphMode(FontMode::TEXT_INVERTED_LARGE)
        ->drawCenteredText(nameLine.c_str(), 40)
        ->render();
}

void ScoringState::onControllerCommandReceived(ControllerCommand command) {
    if (command.command != ControllerCmd::INTERACTION_REQUEST) return;
    if (phase_ != ScoringPhase::NAME_ENTRY) return;

    LOG_W(TAG, "ControllerCmd: button=%d interaction=%d",
          static_cast<int>(command.buttonId),
          static_cast<int>(command.interactionId));

    // PRESS fires on button-down — fastest possible response over the wireless channel.
    if (command.interactionId != ButtonInteraction::PRESS) return;

    if (command.buttonId == ButtonIdentifier::PRIMARY_BUTTON) {
        // TOP button: cycle letter upward A→B→…→Z→A.
        char& c = nameChars_[currentColumn_];
        c = (c >= kLastChar) ? kFirstChar : static_cast<char>(c + 1);
        LOG_W(TAG, "Letter[%d] up: %c", currentColumn_, c);
    } else if (command.buttonId == ButtonIdentifier::SECONDARY_BUTTON) {
        // BOTTOM button: cycle letter downward Z→Y→…→A→Z.
        char& c = nameChars_[currentColumn_];
        c = (c <= kFirstChar) ? kLastChar : static_cast<char>(c - 1);
        LOG_W(TAG, "Letter[%d] down: %c", currentColumn_, c);
    }
}
