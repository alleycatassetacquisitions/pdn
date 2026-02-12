#include "game/breach-defense/breach-defense.hpp"
#include "game/breach-defense/breach-defense-states.hpp"

void BreachDefense::populateStateMap() {
    BreachDefenseIntro* intro = new BreachDefenseIntro(this);
    BreachDefenseWin* win = new BreachDefenseWin(this);
    BreachDefenseLose* lose = new BreachDefenseLose(this);

    // Stub: intro auto-transitions to win
    intro->addTransition(
        new StateTransition(
            std::bind(&BreachDefenseIntro::transitionToWin, intro),
            win));

    // Standalone mode: win/lose loop back to intro for replay
    win->addTransition(
        new StateTransition(
            std::bind(&BreachDefenseWin::transitionToIntro, win),
            intro));

    lose->addTransition(
        new StateTransition(
            std::bind(&BreachDefenseLose::transitionToIntro, lose),
            intro));

    stateMap.push_back(intro);
    stateMap.push_back(win);
    stateMap.push_back(lose);
}

void BreachDefense::resetGame() {
    MiniGame::resetGame();
    session.reset();
}
