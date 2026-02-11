#pragma once

#include "state/state-machine.hpp"
#include "device/device-types.hpp"

constexpr int FDN_GAME_APP_ID = 10;

class FdnGame : public StateMachine {
public:
    FdnGame(GameType gameType, KonamiButton reward);
    ~FdnGame() override;

    void populateStateMap() override;

    GameType getGameType() const { return gameType; }
    KonamiButton getReward() const { return reward; }

    bool getLastResult() const { return lastResult; }
    void setLastResult(bool won) { lastResult = won; }

    int getLastScore() const { return lastScore; }
    void setLastScore(int score) { lastScore = score; }

private:
    GameType gameType;
    KonamiButton reward;
    bool lastResult = false;
    int lastScore = 0;
};
