#include "game/cipher-path/cipher-path.hpp"
#include "game/cipher-path/cipher-path-states.hpp"

void CipherPath::populateStateMap() {
    CipherPathIntro* intro = new CipherPathIntro(this);
    CipherPathWin* win = new CipherPathWin(this);
    CipherPathLose* lose = new CipherPathLose(this);

    // Stub: intro auto-transitions to win
    intro->addTransition(
        new StateTransition(
            std::bind(&CipherPathIntro::transitionToWin, intro),
            win));

    // Standalone mode: win/lose loop back to intro for replay
    win->addTransition(
        new StateTransition(
            std::bind(&CipherPathWin::transitionToIntro, win),
            intro));

    lose->addTransition(
        new StateTransition(
            std::bind(&CipherPathLose::transitionToIntro, lose),
            intro));

    stateMap.push_back(intro);
    stateMap.push_back(win);
    stateMap.push_back(lose);
}

void CipherPath::resetGame() {
    MiniGame::resetGame();
    session.reset();
}
