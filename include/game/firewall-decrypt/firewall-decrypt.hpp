#pragma once

#include "game/minigame.hpp"
#include "utils/simple-timer.hpp"
#include <vector>
#include <string>
#include <cstdlib>

/*
 * Firewall Decrypt configuration — all tuning variables for difficulty.
 * EASY: 5 candidates, 3 rounds, obvious decoys, no timer.
 * HARD: 10 candidates, 4 rounds, subtle decoys, 15s per round.
 */
struct FirewallDecryptConfig {
    int numCandidates = 5;
    int numRounds = 3;
    float similarity = 0.2f;       // 0.0 = random decoys, 1.0 = identical
    int timeLimitMs = 0;           // 0 = no time limit
    unsigned long rngSeed = 0;
    bool managedMode = false;
};

/*
 * Session state for a game in progress.
 * Reset between games (on lose/restart).
 */
struct FirewallDecryptSession {
    std::string target;                     // The MAC address to find
    std::vector<std::string> candidates;    // Scrollable list (includes target)
    int targetIndex = 0;                    // Index of target in candidates
    int currentRound = 0;
    int score = 0;
    int selectedIndex = -1;                 // Player's selection (-1 = none)
    bool timedOut = false;                  // Whether the round timed out

    void reset() {
        target.clear();
        candidates.clear();
        targetIndex = 0;
        currentRound = 0;
        score = 0;
        selectedIndex = -1;
        timedOut = false;
    }
};

constexpr int FIREWALL_DECRYPT_APP_ID = 3;

/*
 * Firewall Decrypt — Find the matching MAC address in a list of decoys.
 *
 * The device shows a target address at the top of the screen, and a
 * scrollable list of candidate addresses below. Player scrolls with
 * primary button and confirms with secondary. Correct match advances
 * to next round. Wrong match = game over.
 *
 * Difficulty controls the number of candidates, how similar decoys
 * are to the target, and an optional per-round time limit.
 *
 * In standalone mode, loops back to intro. In managed mode (via FDN),
 * terminal states call Device::returnToPreviousApp().
 */
class FirewallDecrypt : public MiniGame {
public:
    explicit FirewallDecrypt(FirewallDecryptConfig config);
    void populateStateMap() override;
    void resetGame() override;

    FirewallDecryptConfig& getConfig() { return config; }
    FirewallDecryptSession& getSession() { return session; }

    /*
     * Generate a random MAC address (4 hex pairs, e.g. "A4:7E:3B:C1").
     * Uses seeded rand() for determinism in tests.
     */
    std::string generateAddress();

    /*
     * Generate a decoy that's similar to the target.
     * similarity controls how many pairs are kept from the target.
     * 0.0 = fully random, 1.0 = nearly identical (1 pair different).
     */
    std::string generateDecoy(const std::string& target, float similarity);

    /*
     * Set up a new round: generate target, decoys, and shuffle.
     */
    void setupRound();

    void seedRng();

private:
    FirewallDecryptConfig config;
    FirewallDecryptSession session;
};
