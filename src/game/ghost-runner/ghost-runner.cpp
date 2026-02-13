#include "game/ghost-runner/ghost-runner.hpp"
#include "game/ghost-runner/ghost-runner-states.hpp"

void GhostRunner::populateStateMap() {
    GhostRunnerIntro* intro = new GhostRunnerIntro(this);
    GhostRunnerWin* win = new GhostRunnerWin(this);
    GhostRunnerLose* lose = new GhostRunnerLose(this);

    // Stub: intro auto-transitions to win
    intro->addTransition(
        new StateTransition(
            std::bind(&GhostRunnerIntro::transitionToWin, intro),
            win));

    // Standalone mode: win/lose loop back to intro for replay
    win->addTransition(
        new StateTransition(
            std::bind(&GhostRunnerWin::transitionToIntro, win),
            intro));

    lose->addTransition(
        new StateTransition(
            std::bind(&GhostRunnerLose::transitionToIntro, lose),
            intro));

    stateMap.push_back(intro);
    stateMap.push_back(win);
    stateMap.push_back(lose);
}

void GhostRunner::resetGame() {
    MiniGame::resetGame();
    session.reset();
}
