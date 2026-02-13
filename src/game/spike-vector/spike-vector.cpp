#include "game/spike-vector/spike-vector.hpp"
#include "game/spike-vector/spike-vector-states.hpp"

void SpikeVector::populateStateMap() {
    SpikeVectorIntro* intro = new SpikeVectorIntro(this);
    SpikeVectorShow* show = new SpikeVectorShow(this);
    SpikeVectorGameplay* gameplay = new SpikeVectorGameplay(this);
    SpikeVectorEvaluate* evaluate = new SpikeVectorEvaluate(this);
    SpikeVectorWin* win = new SpikeVectorWin(this);
    SpikeVectorLose* lose = new SpikeVectorLose(this);

    // Intro -> Show
    intro->addTransition(
        new StateTransition(
            std::bind(&SpikeVectorIntro::transitionToShow, intro),
            show));

    // Show -> Gameplay
    show->addTransition(
        new StateTransition(
            std::bind(&SpikeVectorShow::transitionToGameplay, show),
            gameplay));

    // Gameplay -> Evaluate
    gameplay->addTransition(
        new StateTransition(
            std::bind(&SpikeVectorGameplay::transitionToEvaluate, gameplay),
            evaluate));

    // Evaluate -> Show (next wave)
    evaluate->addTransition(
        new StateTransition(
            std::bind(&SpikeVectorEvaluate::transitionToShow, evaluate),
            show));

    // Evaluate -> Win
    evaluate->addTransition(
        new StateTransition(
            std::bind(&SpikeVectorEvaluate::transitionToWin, evaluate),
            win));

    // Evaluate -> Lose
    evaluate->addTransition(
        new StateTransition(
            std::bind(&SpikeVectorEvaluate::transitionToLose, evaluate),
            lose));

    // Standalone mode: win/lose loop back to intro for replay
    win->addTransition(
        new StateTransition(
            std::bind(&SpikeVectorWin::transitionToIntro, win),
            intro));

    lose->addTransition(
        new StateTransition(
            std::bind(&SpikeVectorLose::transitionToIntro, lose),
            intro));

    // Push order: intro(0), show(1), gameplay(2), evaluate(3), win(4), lose(5)
    stateMap.push_back(intro);
    stateMap.push_back(show);
    stateMap.push_back(gameplay);
    stateMap.push_back(evaluate);
    stateMap.push_back(win);
    stateMap.push_back(lose);
}

void SpikeVector::resetGame() {
    MiniGame::resetGame();
    session.reset();
}

void SpikeVector::seedRng() {
    if (config.rngSeed != 0) {
        srand(static_cast<unsigned int>(config.rngSeed));
    } else {
        srand(static_cast<unsigned int>(0));
    }
}
