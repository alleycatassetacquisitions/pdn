#include "game/ghost-runner/ghost-runner.hpp"
#include "game/ghost-runner/ghost-runner-states.hpp"
#include "game/ghost-runner/ghost-runner-resources.hpp"

/*
 * State map push order determines skipToState indices:
 * 0=Intro, 1=Show, 2=Gameplay, 3=Evaluate, 4=Win, 5=Lose
 */
void GhostRunner::populateStateMap() {
    seedRng();

    GhostRunnerIntro* intro = new GhostRunnerIntro(this);
    GhostRunnerShow* show = new GhostRunnerShow(this);
    GhostRunnerGameplay* gameplay = new GhostRunnerGameplay(this);
    GhostRunnerEvaluate* evaluate = new GhostRunnerEvaluate(this);
    GhostRunnerWin* win = new GhostRunnerWin(this);
    GhostRunnerLose* lose = new GhostRunnerLose(this);

    // Intro -> Show
    intro->addTransition(
        new StateTransition(
            std::bind(&GhostRunnerIntro::transitionToShow, intro),
            show));

    // Show -> Gameplay
    show->addTransition(
        new StateTransition(
            std::bind(&GhostRunnerShow::transitionToGameplay, show),
            gameplay));

    // Gameplay -> Evaluate
    gameplay->addTransition(
        new StateTransition(
            std::bind(&GhostRunnerGameplay::transitionToEvaluate, gameplay),
            evaluate));

    // Evaluate -> Show (next round)
    evaluate->addTransition(
        new StateTransition(
            std::bind(&GhostRunnerEvaluate::transitionToShow, evaluate),
            show));

    // Evaluate -> Win
    evaluate->addTransition(
        new StateTransition(
            std::bind(&GhostRunnerEvaluate::transitionToWin, evaluate),
            win));

    // Evaluate -> Lose
    evaluate->addTransition(
        new StateTransition(
            std::bind(&GhostRunnerEvaluate::transitionToLose, evaluate),
            lose));

    // Standalone mode: win/lose loop back to intro for replay
    win->addTransition(
        new StateTransition(
            std::bind(&GhostRunnerWin::transitionToIntro, win),
            intro));

    lose->addTransition(
        new StateTransition(
            std::bind(&GhostRunnerLose::transitionToIntro, lose),
            intro));

    stateMap.push_back(intro);      // index 0
    stateMap.push_back(show);       // index 1
    stateMap.push_back(gameplay);   // index 2
    stateMap.push_back(evaluate);   // index 3
    stateMap.push_back(win);        // index 4
    stateMap.push_back(lose);       // index 5
}

void GhostRunner::resetGame() {
    MiniGame::resetGame();
    session.reset();
}

void GhostRunner::seedRng() {
    if (config.rngSeed != 0) {
        srand(static_cast<unsigned int>(config.rngSeed));
    } else {
        srand(static_cast<unsigned int>(0));
    }
}
