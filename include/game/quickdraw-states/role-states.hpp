#pragma once

#include "game/player.hpp"
#include "utils/simple-timer.hpp"
#include "state/state.hpp"

/*
 * ChooseRoleState - Player selects Hunter or Bounty role
 */
class ChooseRoleState : public State {
public:
    explicit ChooseRoleState(Player* player);
    ~ChooseRoleState();

    void onStateMounted(Device *PDN) override;
    void onStateLoop(Device *PDN) override;
    void onStateDismounted(Device *PDN) override;
    bool transitionToAllegiancePicker();
    void renderUi(Device *PDN);

private:
    Player* player;
    bool transitionToAllegiancePickerState = false;
    bool displayIsDirty = false;
    bool hunterSelected = true;
};

/*
 * WelcomeMessage - Brief welcome screen after role selection
 */
class WelcomeMessage : public State {
public:
    explicit WelcomeMessage(Player* player);
    ~WelcomeMessage();

    void onStateMounted(Device *PDN) override;
    void onStateLoop(Device *PDN) override;
    void onStateDismounted(Device *PDN) override;
    void renderWelcomeMessage(Device *PDN);
    bool transitionToGameplay();

private:
    Player* player;
    SimpleTimer welcomeMessageTimer;
    bool transitionToAwakenSequenceState = false;
    const int WELCOME_MESSAGE_TIMEOUT = 5000;
};
