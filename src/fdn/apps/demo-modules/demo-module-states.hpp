#pragma once

#include "state/state.hpp"
#include "device/fdn.hpp"
#include "device/drivers/display.hpp"
#include "wireless/controller-wireless-manager.hpp"
#include "utils/simple-timer.hpp"
#include <string>

enum DemoModuleStateId {
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
    explicit TutorialState();
    ~TutorialState();

    void onStateMounted(FDN* fdn) override;
    void onStateLoop(FDN* fdn) override;
    void onStateDismounted(FDN* fdn) override;

    bool transitionToMainMenu();
};

class GameState : public TypedState<FDN> {
public:
    explicit GameState(int* primaryScore, int* secondaryScore,
                       const std::string* primaryLabel, const std::string* secondaryLabel);
    ~GameState();

    void onStateMounted(FDN* fdn) override;
    void onStateLoop(FDN* fdn) override;
    void onStateDismounted(FDN* fdn) override;

    bool transitionToScoring();

private:
    int* primaryScore_;
    int* secondaryScore_;
    const std::string* primaryLabel_;
    const std::string* secondaryLabel_;

    static constexpr unsigned long kGameDurationMs = 5000;

    bool transitionToScoringState = false;
    SimpleTimer gameDurationTimer_;

    void onControllerCommandReceived(ControllerCommand command);
};

class ScoringState : public TypedState<FDN> {
public:
    explicit ScoringState(ControllerWirelessManager* controllerWirelessManager,
                          int* primaryScore, int* secondaryScore,
                          const std::string* primaryLabel, const std::string* secondaryLabel,
                          const std::string* thanksMessageLine2,
                          bool dualScoreDisplay,
                          bool showFarewellMessage);
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
    static constexpr unsigned long kShowScoreDurationMs            = 3000;
    static constexpr unsigned long kThanksDurationMs             = 3000;
    static constexpr unsigned long kFarewellDurationMs           = 3000;
    static constexpr unsigned long kNameEntryInactivityTimeoutMs = 30000;

    ControllerWirelessManager* controllerWirelessManager_;
    int* primaryScore_;
    int* secondaryScore_;
    const std::string* primaryLabel_;
    const std::string* secondaryLabel_;
    const std::string* thanksMessageLine2_;
    bool dualScoreDisplay_;
    bool showFarewellMessage_;

    ScoringPhase phase_       = ScoringPhase::SHOW_SCORE;
    SimpleTimer  phaseTimer_;
    SimpleTimer  inactivityTimer_;
    char nameChars_[kNumNameChars] = {'A', 'A', 'A'};
    int  currentColumn_       = 0;
    bool readyToTransition_   = false;
    FDN* fdn_                 = nullptr;

    void renderScoreIntroScreen(FDN* fdn);
    void renderScoringScreen(FDN* fdn);
    void renderMessageScreen(FDN* fdn, const char* line1, const char* line2 = nullptr, const char* line3 = nullptr);
    void onControllerCommandReceived(ControllerCommand command);
    void resetNameEntryInactivityTimer();
    void beginNameEntry(FDN* fdn);
    void endNameEntry(FDN* fdn);
};
