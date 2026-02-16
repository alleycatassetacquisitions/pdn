#include "game/firewall-decrypt/firewall-decrypt.hpp"
#include "game/firewall-decrypt/firewall-decrypt-states.hpp"
#include "game/firewall-decrypt/firewall-decrypt-resources.hpp"
#include <cstdio>
#include <algorithm>

FirewallDecrypt::FirewallDecrypt(FirewallDecryptConfig config) :
    MiniGame(FIREWALL_DECRYPT_APP_ID, GameType::FIREWALL_DECRYPT, "FIREWALL DECRYPT"),
    config(config)
{
}

void FirewallDecrypt::populateStateMap() {
    seedRng(config.rngSeed);

    DecryptIntro* intro = new DecryptIntro(this);
    DecryptScan* scan = new DecryptScan(this);
    DecryptEvaluate* evaluate = new DecryptEvaluate(this);
    DecryptWin* win = new DecryptWin(this);
    DecryptLose* lose = new DecryptLose(this);

    intro->addTransition(
        new StateTransition(
            std::bind(&DecryptIntro::transitionToScan, intro),
            scan));

    scan->addTransition(
        new StateTransition(
            std::bind(&DecryptScan::transitionToEvaluate, scan),
            evaluate));

    evaluate->addTransition(
        new StateTransition(
            std::bind(&DecryptEvaluate::transitionToScan, evaluate),
            scan));

    evaluate->addTransition(
        new StateTransition(
            std::bind(&DecryptEvaluate::transitionToWin, evaluate),
            win));

    evaluate->addTransition(
        new StateTransition(
            std::bind(&DecryptEvaluate::transitionToLose, evaluate),
            lose));

    win->addTransition(
        new StateTransition(
            std::bind(&DecryptWin::transitionToIntro, win),
            intro));

    lose->addTransition(
        new StateTransition(
            std::bind(&DecryptLose::transitionToIntro, lose),
            intro));

    stateMap.push_back(intro);
    stateMap.push_back(scan);
    stateMap.push_back(evaluate);
    stateMap.push_back(win);
    stateMap.push_back(lose);
}

void FirewallDecrypt::resetGame() {
    MiniGame::resetGame();
    session.reset();
}

std::string FirewallDecrypt::generateAddress() {
    char addr[12];
    snprintf(addr, sizeof(addr), "%02X:%02X:%02X:%02X",
             rand() % 256, rand() % 256, rand() % 256, rand() % 256);
    return std::string(addr);
}

std::string FirewallDecrypt::generateDecoy(const std::string& target, float similarity) {
    // Parse target into 4 bytes
    unsigned int bytes[4] = {0, 0, 0, 0};
    sscanf(target.c_str(), "%02X:%02X:%02X:%02X",
           &bytes[0], &bytes[1], &bytes[2], &bytes[3]);

    // Determine how many pairs to keep (higher similarity = more kept)
    int pairsToKeep = static_cast<int>(similarity * 4.0f);
    if (pairsToKeep >= 4) pairsToKeep = 3;  // Always change at least 1 pair

    // Shuffle which pairs to change
    bool keep[4] = {false, false, false, false};
    int kept = 0;
    // Keep first N pairs (simple deterministic approach)
    for (int i = 0; i < 4 && kept < pairsToKeep; i++) {
        keep[i] = true;
        kept++;
    }

    char addr[12];
    unsigned int d[4];
    for (int i = 0; i < 4; i++) {
        if (keep[i]) {
            d[i] = bytes[i];
        } else {
            // Generate different byte
            do {
                d[i] = static_cast<unsigned int>(rand() % 256);
            } while (d[i] == bytes[i]);
        }
    }
    snprintf(addr, sizeof(addr), "%02X:%02X:%02X:%02X", d[0], d[1], d[2], d[3]);
    return std::string(addr);
}

void FirewallDecrypt::setupRound() {
    session.target = generateAddress();
    session.candidates.clear();

    // Target is always at index 0 (banner position)
    session.candidates.push_back(session.target);
    session.targetIndex = 0;

    // Generate decoys
    for (int i = 1; i < config.numCandidates; i++) {
        std::string decoy;
        // Ensure no duplicate candidates
        int attempts = 0;
        do {
            decoy = generateDecoy(session.target, config.similarity);
            attempts++;
        } while (decoy == session.target && attempts < 10);
        session.candidates.push_back(decoy);
    }

    // Shuffle only the decoys (indices 1..N), keep target at 0
    if (session.candidates.size() > 2) {
        auto first = session.candidates.begin() + 1;
        auto last = session.candidates.end();
        // Simple Fisher-Yates shuffle for decoys
        for (auto it = first; it != last - 1; ++it) {
            int offset = rand() % (last - it);
            std::swap(*it, *(it + offset));
        }
    }

    // Reset cursor to position 1 (first candidate after banner)
    session.cursorIndex = 1;
    session.viewStart = 0;
}
