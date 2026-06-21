#pragma once

#include "state/state.hpp"
#include "device/fdn.hpp"
#include "device/drivers/display.hpp"
#include "wireless/controller-wireless-manager.hpp"

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
