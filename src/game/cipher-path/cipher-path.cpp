#include "game/cipher-path/cipher-path.hpp"
#include "game/cipher-path/cipher-path-states.hpp"

void CipherPath::populateStateMap() {
    seedRng(config.rngSeed);

    CipherPathIntro* intro = new CipherPathIntro(this);
    CipherPathShow* show = new CipherPathShow(this);
    CipherPathGameplay* gameplay = new CipherPathGameplay(this);
    CipherPathEvaluate* evaluate = new CipherPathEvaluate(this);
    CipherPathWin* win = new CipherPathWin(this);
    CipherPathLose* lose = new CipherPathLose(this);

    // Intro -> Show
    intro->addTransition(
        new StateTransition(
            std::bind(&CipherPathIntro::transitionToShow, intro),
            show));

    // Show -> Gameplay
    show->addTransition(
        new StateTransition(
            std::bind(&CipherPathShow::transitionToGameplay, show),
            gameplay));

    // Gameplay -> Evaluate
    gameplay->addTransition(
        new StateTransition(
            std::bind(&CipherPathGameplay::transitionToEvaluate, gameplay),
            evaluate));

    // Evaluate -> Show (next round)
    evaluate->addTransition(
        new StateTransition(
            std::bind(&CipherPathEvaluate::transitionToShow, evaluate),
            show));

    // Evaluate -> Win
    evaluate->addTransition(
        new StateTransition(
            std::bind(&CipherPathEvaluate::transitionToWin, evaluate),
            win));

    // Evaluate -> Lose
    evaluate->addTransition(
        new StateTransition(
            std::bind(&CipherPathEvaluate::transitionToLose, evaluate),
            lose));

    // Standalone mode: win/lose loop back to intro for replay
    win->addTransition(
        new StateTransition(
            std::bind(&CipherPathWin::transitionToIntro, win),
            intro));

    lose->addTransition(
        new StateTransition(
            std::bind(&CipherPathLose::transitionToIntro, lose),
            intro));

    // Push order: intro(0), show(1), gameplay(2), evaluate(3), win(4), lose(5)
    stateMap.push_back(intro);
    stateMap.push_back(show);
    stateMap.push_back(gameplay);
    stateMap.push_back(evaluate);
    stateMap.push_back(win);
    stateMap.push_back(lose);
}

void CipherPath::resetGame() {
    MiniGame::resetGame();
    session.reset();
}

void CipherPath::generateCipher() {
    for (int i = 0; i < config.gridSize; i++) {
        session.cipher[i] = rand() % 2;  // 0 = UP correct, 1 = DOWN correct
    }
}
