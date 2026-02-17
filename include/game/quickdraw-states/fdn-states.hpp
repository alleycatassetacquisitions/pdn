#pragma once

#include "game/player.hpp"
#include "utils/simple-timer.hpp"
#include "state/state.hpp"
#include "device/device-types.hpp"
#include <string>
#include <memory>

// Forward declarations
class ProgressManager;
class FdnResultManager;
class DifficultyScaler;

/*
 * FdnDetected — Player detected an FDN broadcast on serial.
 * Performs handshake (sends smac, waits for fack), then configures
 * and launches Signal Echo via setActiveApp(). Quickdraw pauses at
 * this state; on resume, transitions to FdnComplete.
 */
class FdnDetected : public State {
public:
    FdnDetected(Player* player, DifficultyScaler* scaler);
    ~FdnDetected();

    void onStateMounted(Device* PDN) override;
    void onStateLoop(Device* PDN) override;
    void onStateDismounted(Device* PDN) override;
    std::unique_ptr<Snapshot> onStatePaused(Device* PDN) override;
    void onStateResumed(Device* PDN, Snapshot* snapshot) override;

    bool transitionToIdle();
    bool transitionToFdnComplete();
    bool transitionToReencounter();
    bool transitionToKonamiPuzzle();
    bool transitionToConnectionLost();

private:
    Player* player;
    DifficultyScaler* scaler;
    SimpleTimer timeoutTimer;
    static constexpr int TIMEOUT_MS = 10000;
    bool transitionToIdleState = false;
    bool transitionToFdnCompleteState = false;
    bool transitionToReencounterState = false;
    bool transitionToKonamiPuzzleState = false;
    bool transitionToConnectionLostState = false;
    bool fackReceived = false;
    bool macSent = false;
    bool handshakeComplete = false;
    bool gameLaunched = false;
    std::string fdnMessage;
    GameType pendingGameType = GameType::QUICKDRAW;
    KonamiButton pendingReward = KonamiButton::UP;
};

struct FdnDetectedSnapshot : public Snapshot {
    GameType gameType = GameType::QUICKDRAW;
    KonamiButton reward = KonamiButton::UP;
    bool handshakeComplete = false;
    bool gameLaunched = false;
};

/*
 * FdnReencounter -- Prompt shown when player re-encounters an FDN
 * they have already beaten. Offers HARD / EASY / SKIP options.
 * On confirm, configures and launches the minigame with the chosen
 * difficulty. On SKIP or timeout, returns to Idle.
 * After minigame completes, transitions to FdnComplete.
 */
class FdnReencounter : public State {
public:
    FdnReencounter(Player* player, DifficultyScaler* scaler);
    ~FdnReencounter();

    void onStateMounted(Device* PDN) override;
    void onStateLoop(Device* PDN) override;
    void onStateDismounted(Device* PDN) override;
    std::unique_ptr<Snapshot> onStatePaused(Device* PDN) override;
    void onStateResumed(Device* PDN, Snapshot* snapshot) override;

    bool transitionToIdle();
    bool transitionToFdnComplete();

private:
    Player* player;
    DifficultyScaler* scaler;
    SimpleTimer timeoutTimer;
    static constexpr int TIMEOUT_MS = 15000;
    bool transitionToIdleState = false;
    bool transitionToFdnCompleteState = false;
    bool pendingLaunch = false;
    bool gameLaunched = false;

    // Prompt state
    int cursorIndex = 0;  // 0=HARD, 1=EASY, 2=SKIP
    static constexpr int OPTION_COUNT = 3;
    bool fullyCompleted = false;

    // Game data (read from Player on mount)
    GameType pendingGameType = GameType::QUICKDRAW;
    KonamiButton pendingReward = KonamiButton::UP;

    void renderUi(Device* PDN);
    void launchGame(Device* PDN);
};

struct FdnReencounterSnapshot : public Snapshot {
    bool gameLaunched = false;
};

/*
 * FdnComplete — Displays the outcome of the minigame.
 * Reads the outcome from Signal Echo via getApp(). On win, unlocks
 * the Konami button reward and saves progress. Transitions to Idle.
 */
class FdnComplete : public State {
public:
    FdnComplete(Player* player, ProgressManager* progressManager, FdnResultManager* fdnResultManager, DifficultyScaler* scaler);
    ~FdnComplete();

    void onStateMounted(Device* PDN) override;
    void onStateLoop(Device* PDN) override;
    void onStateDismounted(Device* PDN) override;

    bool transitionToColorPrompt();
    bool transitionToBoonAwarded();
    bool transitionToIdle();

private:
    Player* player;
    ProgressManager* progressManager;
    DifficultyScaler* scaler;
    FdnResultManager* fdnResultManager;
    SimpleTimer displayTimer;
    static constexpr int DISPLAY_DURATION_MS = 3000;
    bool transitionToIdleState = false;
    bool transitionToColorPromptState = false;
    bool transitionToBoonAwardedState = false;
};
