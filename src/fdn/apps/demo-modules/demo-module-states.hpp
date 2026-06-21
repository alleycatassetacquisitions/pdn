#pragma once

#include "state/state.hpp"
#include "device/fdn.hpp"
#include "utils/simple-timer.hpp"
#include "device/drivers/display.hpp"

enum DemoModuleStateId {
    MAIN_MENU,
    TUTORIAL,
    GAME,
    SCORING,
};

constexpr int kDemoStateDisplayMs = 1000;

inline void renderDemoStateLabel(FDN* fdn, const char* label) {
    fdn->getDisplay()
        ->invalidateScreen()
        ->setGlyphMode(FontMode::TEXT)
        ->drawCenteredText(label, 32)
        ->render();
}

class MainMenuState : public TypedState<FDN> {
public:
    explicit MainMenuState();
    ~MainMenuState();

    void onStateMounted(FDN* fdn) override;
    void onStateLoop(FDN* fdn) override;
    void onStateDismounted(FDN* fdn) override;

    bool transitionToTutorial();
    bool transitionToGame();

private:
    SimpleTimer transitionTimer;
    bool goToTutorialNext = true;
};

class TutorialState : public TypedState<FDN> {
public:
    explicit TutorialState();
    ~TutorialState();

    void onStateMounted(FDN* fdn) override;
    void onStateLoop(FDN* fdn) override;
    void onStateDismounted(FDN* fdn) override;

    bool transitionToMainMenu();

private:
    SimpleTimer transitionTimer;
};

class GameState : public TypedState<FDN> {
public:
    explicit GameState();
    ~GameState();

    void onStateMounted(FDN* fdn) override;
    void onStateLoop(FDN* fdn) override;
    void onStateDismounted(FDN* fdn) override;

    bool transitionToScoring();

private:
    SimpleTimer transitionTimer;
};

class ScoringState : public TypedState<FDN> {
public:
    explicit ScoringState();
    ~ScoringState();

    void onStateMounted(FDN* fdn) override;
    void onStateLoop(FDN* fdn) override;
    void onStateDismounted(FDN* fdn) override;

    bool transitionToMainMenu();

private:
    SimpleTimer transitionTimer;
};
