#pragma once

#include "state/state.hpp"
#include "device/fdn.hpp"

enum DemoModuleStateId {
    MAIN_MENU,
    TUTORIAL,
    GAME,
    SCORING,
};

class MainMenuState : public TypedState<FDN> {
public:
    explicit MainMenuState();
    ~MainMenuState();

    void onStateMounted(FDN* fdn) override;
    void onStateLoop(FDN* fdn) override;
    void onStateDismounted(FDN* fdn) override;

    bool transitionToTutorial();
    bool transitionToGame();
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
