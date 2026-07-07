#pragma once

#include "state/state.hpp"
#include "device/fdn.hpp"
#include "device/drivers/display.hpp"
#include "wireless/controller-wireless-manager.hpp"

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
    explicit TutorialState();
    ~TutorialState();

    void onStateMounted(FDN* fdn) override;
    void onStateLoop(FDN* fdn) override;
    void onStateDismounted(FDN* fdn) override;

    bool transitionToMainMenu();
};

class GameState : public TypedState<FDN> {
public:
    explicit GameState();
    ~GameState();

    void onStateMounted(FDN* fdn) override;
    void onStateLoop(FDN* fdn) override;
    void onStateDismounted(FDN* fdn) override;

    bool transitionToScoring();
};

class ScoringState : public TypedState<FDN> {
public:
    explicit ScoringState();
    ~ScoringState();

    void onStateMounted(FDN* fdn) override;
    void onStateLoop(FDN* fdn) override;
    void onStateDismounted(FDN* fdn) override;

    bool transitionToMainMenu();
};
