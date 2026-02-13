#include "game/breach-defense/breach-defense.hpp"
#include "game/breach-defense/breach-defense-states.hpp"

void BreachDefense::populateStateMap() {
    BreachDefenseIntro* intro       = new BreachDefenseIntro(this);
    BreachDefenseShow* show         = new BreachDefenseShow(this);
    BreachDefenseGameplay* gameplay = new BreachDefenseGameplay(this);
    BreachDefenseEvaluate* evaluate = new BreachDefenseEvaluate(this);
    BreachDefenseWin* win           = new BreachDefenseWin(this);
    BreachDefenseLose* lose         = new BreachDefenseLose(this);

    // Intro -> Show
    intro->addTransition(
        new StateTransition(
            std::bind(&BreachDefenseIntro::transitionToShow, intro),
            show));

    // Show -> Gameplay
    show->addTransition(
        new StateTransition(
            std::bind(&BreachDefenseShow::transitionToGameplay, show),
            gameplay));

    // Gameplay -> Evaluate
    gameplay->addTransition(
        new StateTransition(
            std::bind(&BreachDefenseGameplay::transitionToEvaluate, gameplay),
            evaluate));

    // Evaluate -> Show (next threat), Win, or Lose
    evaluate->addTransition(
        new StateTransition(
            std::bind(&BreachDefenseEvaluate::transitionToLose, evaluate),
            lose));

    evaluate->addTransition(
        new StateTransition(
            std::bind(&BreachDefenseEvaluate::transitionToWin, evaluate),
            win));

    evaluate->addTransition(
        new StateTransition(
            std::bind(&BreachDefenseEvaluate::transitionToShow, evaluate),
            show));

    // Standalone mode: win/lose loop back to intro for replay
    win->addTransition(
        new StateTransition(
            std::bind(&BreachDefenseWin::transitionToIntro, win),
            intro));

    lose->addTransition(
        new StateTransition(
            std::bind(&BreachDefenseLose::transitionToIntro, lose),
            intro));

    // Push order: intro(0), show(1), gameplay(2), evaluate(3), win(4), lose(5)
    stateMap.push_back(intro);
    stateMap.push_back(show);
    stateMap.push_back(gameplay);
    stateMap.push_back(evaluate);
    stateMap.push_back(win);
    stateMap.push_back(lose);
}

void BreachDefense::resetGame() {
    MiniGame::resetGame();
    session.reset();
}

void BreachDefense::seedRng() {
    if (config.rngSeed != 0) {
        srand(static_cast<unsigned int>(config.rngSeed));
    } else {
        srand(static_cast<unsigned int>(42));
    }
}
