#pragma once

#include "apps/floaty-boat/barrier-controller.hpp"
#include "state/state.hpp"
#include "device/fdn.hpp"
#include "device/drivers/display.hpp"
#include "wireless/controller-wireless-manager.hpp"
#include "utils/simple-timer.hpp"
#include <string>

enum FloatyBoatStateId {
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

inline void renderTutorialMessage(FDN* fdn, const char* line1, const char* line2 = nullptr, const char* line3 = nullptr) {
    Display* d = fdn->getDisplay();
    d->invalidateScreen()->setGlyphMode(FontMode::TEXT_INVERTED_SMALL);
    if (line3) {
        d->drawCenteredText(line1, 18);
        d->drawCenteredText(line2, 34);
        d->drawCenteredText(line3, 50);
    } else if (line2) {
        d->drawCenteredText(line1, 24);
        d->drawCenteredText(line2, 40);
    } else {
        d->drawCenteredText(line1, 32);
    }
    d->render();
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
    static constexpr unsigned long kGameSelectResendIntervalMs = 2000;

    ControllerWirelessManager* controllerWirelessManager;
    bool transitionToTutorialState = false;
    bool transitionToGameState = false;
    SimpleTimer gameSelectResendTimer;

    void sendGameSelectToConnectedPeers(FDN* fdn);
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
        CONTROLS_LANES,
        CONTROLS_UP,
        CONTROLS_DOWN,
        DEMO_PROMPT,
        DEMO,
        COLLISION,
        TRY_AGAIN,
        NICE_WORK,
        READY_TO_PLAY,
    };

    ControllerWirelessManager* controllerWirelessManager_;
    BarrierController barrierController_;
    Phase phase_ = Phase::WELCOME;
    int currentLane_ = 1;
    int dodgedCount_ = 0;
    bool transitionToMainMenuState_ = false;
    SimpleTimer messageTimer_;
    SimpleTimer collisionTimer_;

    static constexpr unsigned long kMessageDurationMs = 3500;
    static constexpr unsigned long kFeedbackDurationMs = 1500;
    static constexpr unsigned long kCollisionDurationMs = 800;
    static constexpr int kObstaclesToDodge = 5;
    static constexpr int kMaxExplosionRadius = 130;
    static constexpr int kCollisionXThreshold = 16;
    static constexpr int kPlayerX = 2;

    void showCurrentMessage(FDN* fdn);
    void advanceMessagePhase();
    void beginDemo();
    void updateDemo(FDN* fdn);
    void moveLaneUp();
    void moveLaneDown();
    void checkCollision();
    void renderDemoScreen(FDN* fdn);
    void onControllerCommandReceived(ControllerCommand command);
};

class GameState : public TypedState<FDN> {
public:
    explicit GameState(ControllerWirelessManager* controllerWirelessManager, int* primaryScore);
    ~GameState();

    void onStateMounted(FDN* fdn) override;
    void onStateLoop(FDN* fdn) override;
    void onStateDismounted(FDN* fdn) override;

    bool transitionToScoring();

private:
    enum class Phase { READY, PLAYING, COLLISION };

    ControllerWirelessManager* controllerWirelessManager_;
    int* primaryScore_;
    BarrierController barrierController_;

    Phase phase_ = Phase::READY;
    int currentLane_ = 1;
    int liveScore_ = 0;
    bool transitionToScoringState_ = false;
    SimpleTimer readyTimer_;
    SimpleTimer collisionTimer_;

    static constexpr unsigned long kReadyDurationMs = 2000;
    static constexpr unsigned long kCollisionDurationMs = 1000;
    static constexpr int kMaxExplosionRadius = 130;
    static constexpr int kCollisionXThreshold = 16;
    static constexpr int kPlayerX = 2;

    void moveLaneUp();
    void moveLaneDown();
    void checkCollision();
    void renderReadyScreen(FDN* fdn);
    void renderPlayScreen(FDN* fdn);
    void renderPlayer(Display* display);
    void renderBarriers(Display* display);
    void renderScore(Display* display);
    void renderExplosion(Display* display);
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
    void renderMessageScreen(FDN* fdn, const char* line1, const char* line2 = nullptr);
    void onControllerCommandReceived(ControllerCommand command);
};
