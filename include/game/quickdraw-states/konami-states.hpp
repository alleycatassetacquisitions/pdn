#pragma once

#include "game/player.hpp"
#include "utils/simple-timer.hpp"
#include "state/state.hpp"
#include "device/device-types.hpp"
#include <vector>

/*
 * KonamiPuzzle — Meta-game activated when all 7 Konami buttons collected.
 * Player enters the Konami Code: UP UP DOWN DOWN LEFT RIGHT LEFT RIGHT B A START
 * Controls: DOWN cycles inputs, UP stamps selected input into sequence.
 * Correct sequence → Konami Boon awarded. Wrong input → retry prompt.
 */
class KonamiPuzzle : public State {
public:
    explicit KonamiPuzzle(Player* player);
    ~KonamiPuzzle();

    void onStateMounted(Device* PDN) override;
    void onStateLoop(Device* PDN) override;
    void onStateDismounted(Device* PDN) override;
    bool transitionToIdle();

private:
    Player* player;
    SimpleTimer displayTimer;
    bool transitionToIdleState = false;
    bool displayIsDirty = true;
    bool puzzleCompleted = false;
    bool errorShown = false;
    int currentPosition = 0;
    int selectedInputIndex = 0;

    // Helper methods
    void buildTargetSequence();
    void buildUnlockedButtons();
    void renderCodeEntry(Device* PDN);
    void renderSuccess(Device* PDN);
    void renderError(Device* PDN);
    bool checkInput();
    void awardKonamiBoon();

    // Data
    std::vector<KonamiButton> targetSequence;
    std::vector<KonamiButton> enteredSequence;
    std::vector<KonamiButton> unlockedButtons;
};

/*
 * ConnectionLost — Displayed when serial connection is lost during FDN interaction.
 * Shows "SIGNAL LOST" message with fast red LED pulse (3 blinks).
 * After 3 seconds, transitions back to Idle state.
 * Future enhancement: add retry prompt with cached FDN context.
 */
class ConnectionLost : public State {
public:
    explicit ConnectionLost(Player* player);
    ~ConnectionLost();

    void onStateMounted(Device* PDN) override;
    void onStateLoop(Device* PDN) override;
    void onStateDismounted(Device* PDN) override;

    bool transitionToIdle();

private:
    Player* player;
    SimpleTimer displayTimer;
    SimpleTimer ledTimer;
    static constexpr int DISPLAY_DURATION_MS = 3000;
    static constexpr int LED_BLINK_INTERVAL_MS = 150;
    static constexpr int BLINK_COUNT = 3;
    bool transitionToIdleState = false;
    int blinkCount = 0;
};
