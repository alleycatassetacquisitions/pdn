#pragma once

#include "game/minigame.hpp"
#include "game/cipher-path/cipher-path-states.hpp"
#include <cstdlib>

constexpr int CIPHER_PATH_APP_ID = 6;

struct CipherPathConfig {
    int gridSize = 8;             // path length (positions 0 to gridSize-1)
    int moveBudget = 12;          // max moves allowed per round
    int rounds = 3;               // rounds to complete
    unsigned long rngSeed = 0;    // 0 = random, nonzero = deterministic
    bool managedMode = false;
};

struct CipherPathSession {
    int playerPosition = 0;       // current position on path (0 to gridSize-1)
    int movesUsed = 0;            // moves consumed this round
    int currentRound = 0;
    int score = 0;
    bool lastMoveCorrect = false; // for display feedback
    // The cipher: for each position, 0 = UP is correct, 1 = DOWN is correct
    int cipher[16] = {0};        // max grid size 16
    void reset() {
        playerPosition = 0;
        movesUsed = 0;
        currentRound = 0;
        score = 0;
        lastMoveCorrect = false;
        for (int i = 0; i < 16; i++) cipher[i] = 0;
    }
};

inline CipherPathConfig makeCipherPathEasyConfig() {
    CipherPathConfig c;
    c.gridSize = 6;
    c.moveBudget = 12;   // generous budget (2x path length)
    c.rounds = 2;
    return c;
}

inline CipherPathConfig makeCipherPathHardConfig() {
    CipherPathConfig c;
    c.gridSize = 10;
    c.moveBudget = 14;   // tight budget (1.4x path length)
    c.rounds = 4;
    return c;
}

const CipherPathConfig CIPHER_PATH_EASY = makeCipherPathEasyConfig();
const CipherPathConfig CIPHER_PATH_HARD = makeCipherPathHardConfig();

/*
 * Cipher Path -- pathfinding puzzle minigame.
 *
 * Player navigates a linear path from start to exit. Each position has a
 * cipher (correct direction: UP or DOWN). Correct guess advances position,
 * wrong guess wastes a move. Reach the exit within the move budget to win.
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

    void generateCipher();  // fills session.cipher[] with random 0/1 values

private:
    CipherPathConfig config;
    CipherPathSession session;
};
