#include "game/breach-defense/breach-defense.hpp"
#include "game/breach-defense/breach-defense-states.hpp"

void BreachDefense::populateStateMap() {
    seedRng(config.rngSeed);

    BreachDefenseIntro* intro       = new BreachDefenseIntro(this);
    BreachDefenseGameplay* gameplay = new BreachDefenseGameplay(this);
    BreachDefenseWin* win           = new BreachDefenseWin(this);
    BreachDefenseLose* lose         = new BreachDefenseLose(this);

    // Intro -> Gameplay (combo starts)
    intro->addTransition(
        new StateTransition(
            std::bind(&BreachDefenseIntro::transitionToGameplay, intro),
            gameplay));

    // Gameplay -> Win or Lose (combo ends)
    gameplay->addTransition(
        new StateTransition(
            std::bind(&BreachDefenseGameplay::transitionToWin, gameplay),
            win));

    gameplay->addTransition(
        new StateTransition(
            std::bind(&BreachDefenseGameplay::transitionToLose, gameplay),
            lose));

    // Standalone mode: win/lose loop back to intro for replay
    win->addTransition(
        new StateTransition(
            std::bind(&BreachDefenseWin::transitionToIntro, win),
            intro));

    lose->addTransition(
        new StateTransition(
            std::bind(&BreachDefenseLose::transitionToIntro, lose),
            intro));

    // Push order: intro(0), gameplay(1), win(2), lose(3)
    stateMap.push_back(intro);
    stateMap.push_back(gameplay);
    stateMap.push_back(win);
    stateMap.push_back(lose);
}

void BreachDefense::resetGame() {
    MiniGame::resetGame();
    session.reset();
}
