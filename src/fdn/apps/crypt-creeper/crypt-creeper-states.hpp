#pragma once

#include "state/state.hpp"
#include "device/fdn.hpp"
#include "device/drivers/display.hpp"
#include "wireless/controller-wireless-manager.hpp"
#include "utils/simple-timer.hpp"
#include "apps/crypt-creeper/maze-pages.hpp"
#include <string>

enum CryptCreeperStateId {
    MAIN_MENU,
    TUTORIAL,
    GAME,
    SCORING,
};

enum class CreepDirection : uint8_t {
    UP = 0,
    LEFT,
    DOWN,
    RIGHT,
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

class CryptCreeperMainMenuState : public TypedState<FDN> {
public:
    explicit CryptCreeperMainMenuState(ControllerWirelessManager* controllerWirelessManager);
    ~CryptCreeperMainMenuState();

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

class CryptCreeperTutorialState : public TypedState<FDN> {
public:
    explicit CryptCreeperTutorialState(ControllerWirelessManager* controllerWirelessManager);
    ~CryptCreeperTutorialState();

    void onStateMounted(FDN* fdn) override;
    void onStateLoop(FDN* fdn) override;
    void onStateDismounted(FDN* fdn) override;

    bool transitionToMainMenu();

private:
    enum class Phase {
        WELCOME,
        NAVIGATE,
        MOVE,
        CORNER_PAUSE,
        TURN_MESSAGE,
        MAZE,
        NICE_WORK,
        READY_REAL,
    };

    static constexpr unsigned long kMessageDurationMs = 5000;
    static constexpr unsigned long kCornerPauseMs = 1000;
    static constexpr uint8_t kWallHapticDurationTenths = 1;

    ControllerWirelessManager* controllerWirelessManager_;
    FDN* fdn_ = nullptr;

    Phase phase_ = Phase::WELCOME;
    SimpleTimer messageTimer_;
    bool transitionToMainMenuState_ = false;

    int currentPage_ = 0;
    int playerRow_   = 0;
    int playerCol_   = 0;
    CreepDirection direction_ = CreepDirection::RIGHT;
    bool allowTurn_ = false;
    bool pendingTurnMessage_ = false;

    void onControllerCommandReceived(ControllerCommand command);
    void advanceMessagePhase();
    void beginMaze(FDN* fdn);
    void renderMessage(FDN* fdn, const char* line1, const char* line2 = nullptr);
    void renderMaze(FDN* fdn);
    void sendDirectionGlyph();
    void pulseHaptic();
    void rotateDirectionCounterClockwise();
    void attemptMove(FDN* fdn);
    bool tryTransitionOffScreen(int newRow, int newCol);
    CryptCreeperMaze::PageLink resolvePageLink(int newRow, int newCol) const;
    void checkCornerAndGoal(FDN* fdn);
};

class CryptCreeperGameState : public TypedState<FDN> {
public:
    explicit CryptCreeperGameState(ControllerWirelessManager* controllerWirelessManager,
                                   unsigned long* elapsedMs);
    ~CryptCreeperGameState();

    void onStateMounted(FDN* fdn) override;
    void onStateLoop(FDN* fdn) override;
    void onStateDismounted(FDN* fdn) override;

    bool transitionToScoring();

private:
    enum class Phase {
        READY,
        PLAYING,
        WIN,
    };

    static constexpr uint8_t kWallHapticDurationTenths = 1;  // 100 ms
    static constexpr uint8_t kWinAnimationId           = 4;  // BountyWinAnimation on PDN
    static constexpr unsigned long kReadyDurationMs    = 3000;
    static constexpr unsigned long kWinDurationMs      = 3000;

    ControllerWirelessManager* controllerWirelessManager_;
    unsigned long* elapsedMs_;
    FDN* fdn_ = nullptr;

    Phase phase_ = Phase::READY;
    int currentPage_  = 0;
    int playerRow_    = 0;
    int playerCol_    = 0;
    CreepDirection direction_ = CreepDirection::RIGHT;
    bool hasReturnedFromPage4_ = false;
    bool transitionToScoringState_ = false;
    SimpleTimer gameTimer_;
    SimpleTimer readyTimer_;
    SimpleTimer winTimer_;

    void onControllerCommandReceived(ControllerCommand command);
    void rotateDirectionCounterClockwise();
    void attemptMove(FDN* fdn);
    void sendDirectionGlyph();
    void pulseHaptic();
    void renderReadyScreen(FDN* fdn);
    void renderMaze(FDN* fdn);
    void resetGame();
    void beginWinAnimation(FDN* fdn);
    void clearWinAnimation(FDN* fdn);
    bool tryTransitionOffScreen(int newRow, int newCol);
    CryptCreeperMaze::PageLink resolvePageLink(int newRow, int newCol) const;
    void checkGoalReached(FDN* fdn);
};

class CryptCreeperScoringState : public TypedState<FDN> {
public:
    explicit CryptCreeperScoringState(ControllerWirelessManager* controllerWirelessManager,
                                      unsigned long* elapsedMs);
    ~CryptCreeperScoringState();

    void onStateMounted(FDN* fdn) override;
    void onStateLoop(FDN* fdn) override;
    void onStateDismounted(FDN* fdn) override;

    bool transitionToMainMenu();

private:
    enum class ScoringPhase {
        SHOW_SCORE,
        NAME_ENTRY,
        THANKS,
    };

    static constexpr int           kNumNameChars        = 3;
    static constexpr char          kFirstChar           = 'A';
    static constexpr char          kLastChar            = 'Z';
    static constexpr unsigned long kShowScoreDurationMs = 3000;
    static constexpr unsigned long kThanksDurationMs    = 3000;

    ControllerWirelessManager* controllerWirelessManager_;
    unsigned long* elapsedMs_;

    ScoringPhase phase_       = ScoringPhase::SHOW_SCORE;
    SimpleTimer  phaseTimer_;
    char         nameChars_[kNumNameChars] = {'A', 'A', 'A'};
    int          currentColumn_     = 0;
    bool         readyToTransition_ = false;

    void renderScoreIntroScreen(FDN* fdn);
    void renderScoringScreen(FDN* fdn);
    void renderMessageScreen(FDN* fdn, const char* line1, const char* line2);
    void onControllerCommandReceived(ControllerCommand command);
};
