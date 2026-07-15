#include "apps/bonk-it/bonk-it-states.hpp"
#include "device/drivers/logger.hpp"

namespace {
static const char* TAG = "TutorialState";

constexpr BonkCue kPracticeSequence[] = {
    BonkCue::UP,
    BonkCue::DOWN,
    BonkCue::X,
    BonkCue::DOWN,
    BonkCue::UP,
};
constexpr size_t kPracticeSequenceLength = sizeof(kPracticeSequence) / sizeof(kPracticeSequence[0]);
}  // namespace

bool TutorialState::isAutoAdvanceMessagePhase(Phase phase) {
    switch (phase) {
        case Phase::WELCOME:
        case Phase::INTRO_GAME:
        case Phase::INSTRUCT_UP:
        case Phase::TRY_UP_PROMPT:
        case Phase::INSTRUCT_DOWN:
        case Phase::INSTRUCT_X:
        case Phase::CHALLENGE_1:
        case Phase::CHALLENGE_2:
        case Phase::CHALLENGE_3:
        case Phase::READY:
        case Phase::COMPLETE:
            return true;
        default:
            return false;
    }
}

bool TutorialState::isCuePhase(Phase phase) {
    return phase == Phase::CUE_UP || phase == Phase::CUE_DOWN || phase == Phase::CUE_X ||
           phase == Phase::PRACTICE;
}

BonkCue TutorialState::cueForPhase(Phase phase) const {
    switch (phase) {
        case Phase::CUE_UP:
            return BonkCue::UP;
        case Phase::CUE_DOWN:
            return BonkCue::DOWN;
        case Phase::CUE_X:
            return BonkCue::X;
        default:
            return BonkCue::UP;
    }
}

BonkCue TutorialState::currentPracticeCue() const {
    if (practiceIndex_ >= kPracticeSequenceLength) {
        return BonkCue::UP;
    }
    return kPracticeSequence[practiceIndex_];
}

TutorialState::TutorialState(ControllerWirelessManager* controllerWirelessManager)
    : TypedState<FDN>(BonkItStateId::TUTORIAL)
    , controllerWirelessManager_(controllerWirelessManager)
    , session_(controllerWirelessManager) {}

TutorialState::~TutorialState() {}

void TutorialState::onStateMounted(FDN* fdn) {
    LOG_W(TAG, "Mounted");
    phase_                    = Phase::WELCOME;
    resumePhase_              = Phase::WELCOME;
    transitionToMainMenuState_ = false;
    pendingButtonPress_       = false;
    practiceIndex_            = 0;
    session_.reset();
    session_.clearAnimations(fdn);
    showCurrentMessage(fdn);
    restartMessageTimer();

    controllerWirelessManager_->setControllerCommandReceivedCallback(
        std::bind(&TutorialState::onControllerCommandReceived, this, std::placeholders::_1),
        SerialIdentifier::INPUT_JACK);
    controllerWirelessManager_->setControllerCommandReceivedCallback(
        std::bind(&TutorialState::onControllerCommandReceived, this, std::placeholders::_1),
        SerialIdentifier::INPUT_JACK_SECONDARY);
}

void TutorialState::onStateLoop(FDN* fdn) {
    session_.update(fdn);

    if (pendingButtonPress_) {
        pendingButtonPress_ = false;
        if (isCuePhase(phase_)) {
            handleRoundResult(fdn, session_.onButtonPress(pendingButtonId_));
        }
    }

    if (phase_ == Phase::FEEDBACK) {
        feedbackTimer_.updateTime();
        session_.renderFeedbackMessage(fdn, "Nice work!");
        if (feedbackTimer_.expired()) {
            phase_ = resumePhase_;
            if (phase_ == Phase::PRACTICE) {
                if (practiceIndex_ >= kPracticeSequenceLength) {
                    phase_ = Phase::COMPLETE;
                }
                showCurrentMessage(fdn);
                if (phase_ == Phase::PRACTICE) {
                    beginCuePhase(fdn, Phase::PRACTICE);
                } else {
                    restartMessageTimer();
                }
            } else {
                showCurrentMessage(fdn);
                restartMessageTimer();
            }
        }
        return;
    }

    if (phase_ == Phase::TRY_AGAIN) {
        feedbackTimer_.updateTime();
        session_.renderFeedbackMessage(fdn, "Nope! Try again!");
        if (feedbackTimer_.expired()) {
            phase_ = resumePhase_;
            if (isCuePhase(phase_)) {
                beginCuePhase(fdn, phase_);
            }
        }
        return;
    }

    if (isCuePhase(phase_)) {
        messageTimer_.invalidate();
        BonkRoundResult result = session_.pollResult();
        if (result != BonkRoundResult::NONE) {
            handleRoundResult(fdn, result);
        }
        return;
    }

    showCurrentMessage(fdn);

    if (isAutoAdvanceMessagePhase(phase_)) {
        messageTimer_.updateTime();
        if (messageTimer_.expired()) {
            advanceMessagePhase(fdn);
        }
    }
}

void TutorialState::onStateDismounted(FDN* fdn) {
    LOG_W(TAG, "Dismounted");
    messageTimer_.invalidate();
    feedbackTimer_.invalidate();
    session_.clearAnimations(fdn);
    controllerWirelessManager_->clearCallback();
}

bool TutorialState::transitionToMainMenu() {
    return transitionToMainMenuState_;
}

void TutorialState::restartMessageTimer() {
    if (!isAutoAdvanceMessagePhase(phase_)) {
        messageTimer_.invalidate();
        return;
    }
    messageTimer_.setTimer(kMessageAutoAdvanceMs);
}

void TutorialState::advanceMessagePhase(FDN* fdn) {
    messageTimer_.invalidate();

    switch (phase_) {
        case Phase::WELCOME:
            phase_ = Phase::INTRO_GAME;
            break;
        case Phase::INTRO_GAME:
            phase_ = Phase::INSTRUCT_UP;
            break;
        case Phase::INSTRUCT_UP:
            phase_ = Phase::TRY_UP_PROMPT;
            break;
        case Phase::TRY_UP_PROMPT:
            beginCuePhase(fdn, Phase::CUE_UP);
            return;
        case Phase::INSTRUCT_DOWN:
            beginCuePhase(fdn, Phase::CUE_DOWN);
            return;
        case Phase::INSTRUCT_X:
            beginCuePhase(fdn, Phase::CUE_X);
            return;
        case Phase::CHALLENGE_1:
            phase_ = Phase::CHALLENGE_2;
            break;
        case Phase::CHALLENGE_2:
            phase_ = Phase::CHALLENGE_3;
            break;
        case Phase::CHALLENGE_3:
            phase_ = Phase::READY;
            break;
        case Phase::READY:
            practiceIndex_ = 0;
            beginCuePhase(fdn, Phase::PRACTICE);
            return;
        case Phase::COMPLETE:
            transitionToMainMenuState_ = true;
            return;
        default:
            return;
    }

    showCurrentMessage(fdn);
    restartMessageTimer();
}

void TutorialState::beginCuePhase(FDN* fdn, Phase phase) {
    phase_ = phase;
    messageTimer_.invalidate();

    unsigned long duration   = 0;
    bool showTimerBar        = false;
    BonkCue cue              = cueForPhase(phase);

    if (phase == Phase::CUE_X) {
        duration = kTutorialXDurationMs;
    } else if (phase == Phase::PRACTICE) {
        cue           = currentPracticeCue();
        duration      = kPracticeCueDurationMs;
        showTimerBar  = true;
    }

    session_.beginCue(fdn, cue, duration, showTimerBar);
}

void TutorialState::showFeedback(FDN* fdn, const char* message, Phase resumePhase) {
    (void)message;
    resumePhase_ = resumePhase;
    phase_       = Phase::FEEDBACK;
    feedbackTimer_.setTimer(kFeedbackDurationMs);
    session_.renderFeedbackMessage(fdn, "Nice work!");
}

void TutorialState::showTryAgain(FDN* fdn, Phase resumePhase) {
    resumePhase_ = resumePhase;
    phase_       = Phase::TRY_AGAIN;
    feedbackTimer_.setTimer(kTryAgainDurationMs);
    session_.renderFeedbackMessage(fdn, "Nope! Try again!");
}

void TutorialState::handleRoundResult(FDN* fdn, BonkRoundResult result) {
    if (result == BonkRoundResult::NONE) {
        return;
    }

    if (result == BonkRoundResult::CORRECT) {
        session_.pulseCorrectFeedback();

        if (phase_ == Phase::CUE_UP) {
            showFeedback(fdn, "Nice work!", Phase::INSTRUCT_DOWN);
            return;
        }
        if (phase_ == Phase::CUE_DOWN) {
            showFeedback(fdn, "Nice work!", Phase::INSTRUCT_X);
            return;
        }
        if (phase_ == Phase::CUE_X) {
            showFeedback(fdn, "Nice work!", Phase::CHALLENGE_1);
            return;
        }
        if (phase_ == Phase::PRACTICE) {
            practiceIndex_++;
            if (practiceIndex_ >= kPracticeSequenceLength) {
                phase_ = Phase::COMPLETE;
                showCurrentMessage(fdn);
                restartMessageTimer();
            } else {
                beginCuePhase(fdn, Phase::PRACTICE);
            }
            return;
        }
    }

    if (phase_ == Phase::PRACTICE) {
        practiceIndex_ = 0;
        showTryAgain(fdn, Phase::PRACTICE);
        return;
    }

    showTryAgain(fdn, phase_);
}

void TutorialState::showCurrentMessage(FDN* fdn) {
    switch (phase_) {
        case Phase::WELCOME:
            session_.renderTutorialMessage(fdn, "Welcome to", "Bonk It!");
            break;
        case Phase::INTRO_GAME:
            session_.renderTutorialMessage(fdn, "You will respond", "to three different", "visual cues");
            break;
        case Phase::INSTRUCT_UP:
            session_.renderTutorialMessage(
                fdn, "When I show you", "an UP arrow, press", "the top button");
            break;
        case Phase::TRY_UP_PROMPT:
            session_.renderTutorialMessage(fdn, "Go ahead,", "give it a try!");
            break;
        case Phase::INSTRUCT_DOWN:
            session_.renderTutorialMessage(
                fdn, "When I show you", "a DOWN arrow, press", "the bottom button");
            break;
        case Phase::INSTRUCT_X:
            session_.renderTutorialMessage(
                fdn, "Last cue: When", "I show you an X,", "press NO button");
            break;
        case Phase::CHALLENGE_1:
            session_.renderTutorialMessage(fdn, "Now for a", "challenge");
            break;
        case Phase::CHALLENGE_2:
            session_.renderTutorialMessage(
                fdn, "I will show you", "five symbols in", "sequence");
            break;
        case Phase::CHALLENGE_3:
            session_.renderTutorialMessage(
                fdn, "Your job is to", "respond to each", "before time runs out");
            break;
        case Phase::READY:
            session_.renderTutorialMessage(fdn, "Ready?");
            break;
        case Phase::COMPLETE:
            session_.renderTutorialMessage(fdn, "Great work!", "Ready to play?");
            break;
        default:
            break;
    }
}

void TutorialState::onControllerCommandReceived(ControllerCommand command) {
    if (command.command != ControllerCmd::INTERACTION_REQUEST || !command.wifiMacAddrValid) {
        return;
    }

    controllerWirelessManager_->setMacPeer(command.wifiMacAddr);

    if (command.interactionId != ButtonInteraction::PRESS) {
        return;
    }

    pendingButtonPress_ = true;
    pendingButtonId_  = command.buttonId;
}
