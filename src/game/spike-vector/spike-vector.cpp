#include "game/spike-vector/spike-vector.hpp"
#include "game/spike-vector/spike-vector-states.hpp"

void SpikeVector::populateStateMap() {
    SpikeVectorIntro* intro = new SpikeVectorIntro(this);
    SpikeVectorWin* win = new SpikeVectorWin(this);
    SpikeVectorLose* lose = new SpikeVectorLose(this);

    // Stub: intro auto-transitions to win
    intro->addTransition(
        new StateTransition(
            std::bind(&SpikeVectorIntro::transitionToWin, intro),
            win));

    // Standalone mode: win/lose loop back to intro for replay
    win->addTransition(
        new StateTransition(
            std::bind(&SpikeVectorWin::transitionToIntro, win),
            intro));

    lose->addTransition(
        new StateTransition(
            std::bind(&SpikeVectorLose::transitionToIntro, lose),
            intro));

    stateMap.push_back(intro);
    stateMap.push_back(win);
    stateMap.push_back(lose);
}

void SpikeVector::resetGame() {
    MiniGame::resetGame();
    session.reset();
}
