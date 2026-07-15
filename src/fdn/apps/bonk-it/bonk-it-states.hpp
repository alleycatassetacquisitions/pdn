#pragma once

#include "apps/bonk-it/bonk-it-session.hpp"
#include "state/state.hpp"
#include "device/fdn.hpp"
#include "device/drivers/display.hpp"
#include "wireless/controller-wireless-manager.hpp"
#include "utils/simple-timer.hpp"
#include <string>

enum BonkItStateId {
    MAIN_MENU,
    TUTORIAL,
    GAME,
    SCORING,
};

inline void renderDemoStateLabel(FDN* fdn, const char* label) {
    fdn->getDisplay()
        ->invalidateScreen()
        ->setGlyphMode(FontMode::TEXT)
        ->drawCenteredText(label, 32)
        ->render();
}

inline int centeredTextXInHalf(Display* display, const char* text, int halfCenterX) {
    return halfCenterX - display->getTextWidth(text) / 2;
}

inline void renderMainMenuScreen(FDN* fdn, const char* gameTitle) {
    static constexpr const char* kTutorialOption = "TUTORIAL";
    static constexpr const char* kPlayOption = "PLAY";

    constexpr int kLeftButtonCenterX  = 32;
    constexpr int kRightButtonCenterX = 96;
    constexpr int kTitleY             = 28;
    constexpr int kOptionsY           = 56;

    Display* d = fdn->getDisplay();
    d->invalidateScreen()
        ->setGlyphMode(FontMode::TEXT_INVERTED_LARGE)
        ->drawCenteredText(gameTitle, kTitleY)
        ->setGlyphMode(FontMode::TEXT_INVERTED_SMALL)
        ->drawText(kTutorialOption, centeredTextXInHalf(d, kTutorialOption, kLeftButtonCenterX), kOptionsY)
        ->drawText(kPlayOption, centeredTextXInHalf(d, kPlayOption, kRightButtonCenterX), kOptionsY)
        ->render();
}

class MainMenuState : public TypedState<FDN> {
public:
    explicit MainMenuState(ControllerWirelessManager* controllerWirelessManager);
    ~MainMenuState();

    void onStateMounted(FDN* fdn) override;
    void onStateLoop(FDN* fdn) override;
    void onStateDismounted(FDN* fdn) override;

    bool transitionToTutorial();
    bool transitionToGame();

private:
    ControllerWirelessManager* controllerWirelessManager;
    bool transitionToTutorialState = false;
    bool transitionToGameState = false;

    void onControllerCommandReceived(ControllerCommand command);
};

class TutorialState : public TypedState<FDN> {
public:
    explicit TutorialState(ControllerWirelessManager* controllerWirelessManager);
    ~TutorialState();

    void onStateMounted(FDN* fdn) override;
    void onStateLoop(FDN* fdn) override;
    void onStateDismounted(FDN* fdn) override;

    bool transitionToMainMenu();

private:
    enum class Phase {
        WELCOME,
        INTRO_GAME,
        INSTRUCT_UP,
        TRY_UP_PROMPT,
        CUE_UP,
        INSTRUCT_DOWN,
        CUE_DOWN,
        INSTRUCT_X,
        CUE_X,
        CHALLENGE_1,
        CHALLENGE_2,
        CHALLENGE_3,
        READY,
        PRACTICE,
        COMPLETE,
        FEEDBACK,
        TRY_AGAIN,
    };

    ControllerWirelessManager* controllerWirelessManager_;
    BonkItSession session_;
    Phase phase_ = Phase::WELCOME;
    Phase resumePhase_ = Phase::WELCOME;
    bool transitionToMainMenuState_ = false;
    bool pendingButtonPress_ = false;
    ButtonIdentifier pendingButtonId_ = ButtonIdentifier::PRIMARY_BUTTON;
    SimpleTimer messageTimer_;
    SimpleTimer feedbackTimer_;
    size_t practiceIndex_ = 0;

    static constexpr unsigned long kMessageAutoAdvanceMs = 5000;
    static constexpr unsigned long kFeedbackDurationMs     = 1500;
    static constexpr unsigned long kTryAgainDurationMs     = 1500;
    static constexpr unsigned long kTutorialXDurationMs    = 3000;
    static constexpr unsigned long kPracticeCueDurationMs  = 2000;

    static bool isAutoAdvanceMessagePhase(Phase phase);
    static bool isCuePhase(Phase phase);
    BonkCue cueForPhase(Phase phase) const;
    BonkCue currentPracticeCue() const;

    void showCurrentMessage(FDN* fdn);
    void restartMessageTimer();
    void advanceMessagePhase(FDN* fdn);
    void beginCuePhase(FDN* fdn, Phase phase);
    void showFeedback(FDN* fdn, const char* message, Phase resumePhase);
    void showTryAgain(FDN* fdn, Phase resumePhase);
    void handleRoundResult(FDN* fdn, BonkRoundResult result);
    void onControllerCommandReceived(ControllerCommand command);
};

class GameState : public TypedState<FDN> {
public:
    explicit GameState(ControllerWirelessManager* controllerWirelessManager,
                       int* primaryScore);
    ~GameState();

    void onStateMounted(FDN* fdn) override;
    void onStateLoop(FDN* fdn) override;
    void onStateDismounted(FDN* fdn) override;

    bool transitionToScoring();

private:
    enum class Phase { READY, PLAYING, LOSS };

    ControllerWirelessManager* controllerWirelessManager_;
    int* primaryScore_;
    BonkItSession session_;

    Phase phase_ = Phase::READY;
    int roundIndex_ = 0;
    bool transitionToScoringState_ = false;
    bool pendingButtonPress_ = false;
    ButtonIdentifier pendingButtonId_ = ButtonIdentifier::PRIMARY_BUTTON;
    SimpleTimer readyTimer_;

    static constexpr unsigned long kReadyDurationMs = 2000;

    void startPlaying(FDN* fdn);
    void startNextRound(FDN* fdn);
    void handleRoundResult(FDN* fdn, BonkRoundResult result);
    void finalizeScores();
    void onControllerCommandReceived(ControllerCommand command);
};

class ScoringState : public TypedState<FDN> {
public:
    explicit ScoringState(ControllerWirelessManager* controllerWirelessManager,
                          int* primaryScore,
                          const std::string* primaryLabel);
    ~ScoringState();

    void onStateMounted(FDN* fdn) override;
    void onStateLoop(FDN* fdn) override;
    void onStateDismounted(FDN* fdn) override;

    bool transitionToMainMenu();

private:
    enum class ScoringPhase { SHOW_SCORE, NAME_ENTRY, THANKS };

    static constexpr int           kNumNameChars        = 3;
    static constexpr char          kFirstChar           = 'A';
    static constexpr char          kLastChar            = 'Z';
    static constexpr unsigned long kShowScoreDurationMs = 3000;
    static constexpr unsigned long kThanksDurationMs    = 3000;

    ControllerWirelessManager* controllerWirelessManager_;
    int* primaryScore_;
    const std::string* primaryLabel_;

    ScoringPhase phase_       = ScoringPhase::SHOW_SCORE;
    SimpleTimer  phaseTimer_;
    char nameChars_[kNumNameChars] = {'A', 'A', 'A'};
    int  currentColumn_       = 0;
    bool readyToTransition_   = false;

    void renderScoreIntroScreen(FDN* fdn);
    void renderScoringScreen(FDN* fdn);
    void renderMessageScreen(FDN* fdn, const char* line1);
    void renderMessageScreen(FDN* fdn, const char* line1, const char* line2);
    void renderMessageScreen(FDN* fdn, const char* line1, const char* line2, const char* line3);
    void onControllerCommandReceived(ControllerCommand command);
};
