#pragma once

#include "state/state-machine.hpp"
#include "device/device-types.hpp"

class ChallengeGame : public StateMachine {
public:
    ChallengeGame(Device* device, GameType gameType, KonamiButton reward);
    ~ChallengeGame() override;

    void populateStateMap() override;

    GameType getGameType() const { return gameType; }
    KonamiButton getReward() const { return reward; }

    /*
     * Get the last game result.
     * Set by NpcReceiveResult state.
     * true = player won, false = player lost.
     */
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
