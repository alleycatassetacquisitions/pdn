#pragma once

#include "apps/quickdraw-demo/quickdraw-practice.hpp"
#include "state/state.hpp"
#include "device/fdn.hpp"
#include "device/drivers/display.hpp"
#include "wireless/controller-wireless-manager.hpp"
#include "utils/simple-timer.hpp"
#include <string>

enum QuickdrawDemoStateId {
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

inline void renderMainMenuScreen(FDN* fdn, const char* gameTitleLine1, const char* gameTitleLine2) {
    static constexpr const char* kTutorialOption = "TUTORIAL";
    static constexpr const char* kPlayOption = "PLAY";

    constexpr int kLeftButtonCenterX  = 32;
    constexpr int kRightButtonCenterX = 96;
    constexpr int kTitleLine1Y        = 20;
    constexpr int kTitleLine2Y        = 36;
    constexpr int kOptionsY           = 56;

    Display* d = fdn->getDisplay();
    d->invalidateScreen()
        ->setGlyphMode(FontMode::TEXT_INVERTED_LARGE)
        ->drawCenteredText(gameTitleLine1, kTitleLine1Y)
        ->drawCenteredText(gameTitleLine2, kTitleLine2Y)
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
        INTRO_1,
        INTRO_2,
        INTRO_3,
        RULE_1,
        RULE_2,
        RULE_3,
        READY,
        COUNTDOWN,
        TRY_AGAIN,
        WIN_OUTRO_1,
        WIN_OUTRO_2,
    };

    ControllerWirelessManager* controllerWirelessManager_;
    QuickdrawPracticeSession session_;
    Phase phase_ = Phase::INTRO_1;
    bool showingOutcome_ = false;
    bool transitionToMainMenuState_ = false;
    bool pendingBottomButton_ = false;
    SimpleTimer messageTimer_;

    static constexpr unsigned long kMessageAutoAdvanceMs = 3000;

    static bool isMessagePhase(Phase phase);
    static bool phaseWaitsForButton(Phase phase);

    void showCurrentMessage(FDN* fdn);
    void restartMessageTimer();
    void advanceMessagePhase(FDN* fdn);
    void onControllerCommandReceived(ControllerCommand command);
    void handleBottomButton(FDN* fdn);
    void beginReadyCountdown(FDN* fdn);
    void handleMatchOutcome(FDN* fdn);
};

class GameState : public TypedState<FDN> {
public:
    explicit GameState(ControllerWirelessManager* controllerWirelessManager,
                       int* primaryScore,
                       int* secondaryScore);
    ~GameState();

    void onStateMounted(FDN* fdn) override;
    void onStateLoop(FDN* fdn) override;
    void onStateDismounted(FDN* fdn) override;

    bool transitionToScoring();

private:
    ControllerWirelessManager* controllerWirelessManager_;
    int* primaryScore_;
    int* secondaryScore_;
    QuickdrawPracticeSession session_;

    int winCount_ = 0;
    unsigned long reactionTimeTotalMs_ = 0;
    int reactionSampleCount_ = 0;
    bool showingOutcome_ = false;
    bool showingRoundResult_ = false;
    bool transitionToScoringState_ = false;
    QuickdrawPracticeOutcome lastOutcome_ = QuickdrawPracticeOutcome::NONE;
    unsigned long lastReactionMs_ = 0;
    SimpleTimer roundResultTimer_;

    static constexpr unsigned long kRoundResultDisplayMs =
        QuickdrawCountdown::kRoundResultDurationMs;

    void startNextMatch(FDN* fdn);
    void onControllerCommandReceived(ControllerCommand command);
    void handleMatchOutcome(FDN* fdn);
    void finalizeScores();
};

class ScoringState : public TypedState<FDN> {
public:
    explicit ScoringState(ControllerWirelessManager* controllerWirelessManager,
                          int* primaryScore, int* secondaryScore,
                          const std::string* primaryLabel, const std::string* secondaryLabel);
    ~ScoringState();

    void onStateMounted(FDN* fdn) override;
    void onStateLoop(FDN* fdn) override;
    void onStateDismounted(FDN* fdn) override;

    bool transitionToMainMenu();

private:
    enum class ScoringPhase { SHOW_SCORE, NAME_ENTRY, THANKS, FAREWELL };

    static constexpr int           kNumNameChars        = 3;
    static constexpr char          kFirstChar           = 'A';
    static constexpr char          kLastChar            = 'Z';
    static constexpr unsigned long kShowScoreDurationMs = 3000;
    static constexpr unsigned long kThanksDurationMs    = 3000;
    static constexpr unsigned long kFarewellDurationMs  = 3000;

    ControllerWirelessManager* controllerWirelessManager_;
    int* primaryScore_;
    int* secondaryScore_;
    const std::string* primaryLabel_;
    const std::string* secondaryLabel_;

    ScoringPhase phase_       = ScoringPhase::SHOW_SCORE;
    SimpleTimer  phaseTimer_;
    char nameChars_[kNumNameChars] = {'A', 'A', 'A'};
    int  currentColumn_       = 0;
    bool readyToTransition_   = false;

    void renderScoreIntroScreen(FDN* fdn);
    void renderScoringScreen(FDN* fdn);
    void renderMessageScreen(FDN* fdn, const char* line1, const char* line2 = nullptr, const char* line3 = nullptr);
    void onControllerCommandReceived(ControllerCommand command);
};
