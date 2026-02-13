#pragma once

#include "game/minigame.hpp"
#include "game/breach-defense/breach-defense-states.hpp"
#include <cstdlib>

constexpr int BREACH_DEFENSE_APP_ID = 8;

struct BreachDefenseConfig {
    int numLanes = 3;             // number of defense lanes (0 to numLanes-1)
    int threatSpeedMs = 40;       // ms per threat step
    int threatDistance = 100;      // threat travels 0 to threatDistance
    int totalThreats = 8;         // total threats to survive
    int missesAllowed = 2;        // breaches before losing
    unsigned long rngSeed = 0;
    bool managedMode = false;
};

struct BreachDefenseSession {
    int shieldLane = 0;           // player's current shield position
    int threatLane = 0;           // which lane current threat is in
    int threatPosition = 0;       // how far the threat has traveled
    int currentThreat = 0;        // which threat number (0-based)
    int breaches = 0;             // damage taken
    int score = 0;
    bool threatArrived = false;   // true when threat reaches the end
    void reset() {
        shieldLane = 0;
        threatLane = 0;
        threatPosition = 0;
        currentThreat = 0;
        breaches = 0;
        score = 0;
        threatArrived = false;
    }
};

inline BreachDefenseConfig makeBreachDefenseEasyConfig() {
    BreachDefenseConfig c;
    c.numLanes = 3;
    c.threatSpeedMs = 40;
    c.threatDistance = 100;
    c.totalThreats = 6;
    c.missesAllowed = 3;
    return c;
}

inline BreachDefenseConfig makeBreachDefenseHardConfig() {
    BreachDefenseConfig c;
    c.numLanes = 5;
    c.threatSpeedMs = 20;
    c.threatDistance = 100;
    c.totalThreats = 12;
    c.missesAllowed = 1;
    return c;
}

const BreachDefenseConfig BREACH_DEFENSE_EASY = makeBreachDefenseEasyConfig();
const BreachDefenseConfig BREACH_DEFENSE_HARD = makeBreachDefenseHardConfig();

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

    void seedRng();

private:
    BreachDefenseConfig config;
    BreachDefenseSession session;
};
