#pragma once

#include "game/minigame.hpp"
#include "game/cipher-path/cipher-path-states.hpp"

constexpr int CIPHER_PATH_APP_ID = 6;

struct CipherPathConfig {
    int timeLimitMs = 0;
    unsigned long rngSeed = 0;
    bool managedMode = false;
};

struct CipherPathSession {
    int score = 0;
    void reset() { score = 0; }
};

inline CipherPathConfig makeCipherPathEasyConfig() {
    CipherPathConfig c;
    c.timeLimitMs = 0;
    c.rngSeed = 0;
    c.managedMode = false;
    return c;
}

inline CipherPathConfig makeCipherPathHardConfig() {
    CipherPathConfig c;
    c.timeLimitMs = 0;
    c.rngSeed = 0;
    c.managedMode = false;
    return c;
}

const CipherPathConfig CIPHER_PATH_EASY = makeCipherPathEasyConfig();
const CipherPathConfig CIPHER_PATH_HARD = makeCipherPathHardConfig();

/*
 * Cipher Path — stub minigame.
 *
 * Placeholder implementation: shows intro, auto-wins after 2s.
 * Phase 2 replaces with real pathfinding mechanics.
 *
 * In managed mode (via FDN), terminal states call
 * Device::returnToPreviousApp(). In standalone mode, loops to intro.
 */
class CipherPath : public MiniGame {
public:
    explicit CipherPath(CipherPathConfig config) :
        MiniGame(CIPHER_PATH_APP_ID, GameType::CIPHER_PATH, "CIPHER PATH"),
        config(config)
    {
    }

    void populateStateMap() override;
    void resetGame() override;

    CipherPathConfig& getConfig() { return config; }
    CipherPathSession& getSession() { return session; }

private:
    CipherPathConfig config;
    CipherPathSession session;
};
