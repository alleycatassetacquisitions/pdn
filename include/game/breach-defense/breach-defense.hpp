#pragma once

#include "game/minigame.hpp"
#include "game/breach-defense/breach-defense-states.hpp"

constexpr int BREACH_DEFENSE_APP_ID = 8;

struct BreachDefenseConfig {
    int timeLimitMs = 0;
    unsigned long rngSeed = 0;
    bool managedMode = false;
};

struct BreachDefenseSession {
    int score = 0;
    void reset() { score = 0; }
};

inline BreachDefenseConfig makeBreachDefenseEasyConfig() {
    BreachDefenseConfig c;
    c.timeLimitMs = 0;
    c.rngSeed = 0;
    c.managedMode = false;
    return c;
}

inline BreachDefenseConfig makeBreachDefenseHardConfig() {
    BreachDefenseConfig c;
    c.timeLimitMs = 0;
    c.rngSeed = 0;
    c.managedMode = false;
    return c;
}

const BreachDefenseConfig BREACH_DEFENSE_EASY = makeBreachDefenseEasyConfig();
const BreachDefenseConfig BREACH_DEFENSE_HARD = makeBreachDefenseHardConfig();

/*
 * Breach Defense — stub minigame.
 *
 * Placeholder implementation: shows intro, auto-wins after 2s.
 * Phase 2 replaces with real fortress-defense mechanics.
 *
 * In managed mode (via FDN), terminal states call
 * Device::returnToPreviousApp(). In standalone mode, loops to intro.
 */
class BreachDefense : public MiniGame {
public:
    explicit BreachDefense(BreachDefenseConfig config) :
        MiniGame(BREACH_DEFENSE_APP_ID, GameType::BREACH_DEFENSE, "BREACH DEFENSE"),
        config(config)
    {
    }

    void populateStateMap() override;
    void resetGame() override;

    BreachDefenseConfig& getConfig() { return config; }
    BreachDefenseSession& getSession() { return session; }

private:
    BreachDefenseConfig config;
    BreachDefenseSession session;
};
