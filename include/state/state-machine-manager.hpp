#pragma once

#include "state/state-machine.hpp"

/*
 * SwappableOutcome — lightweight result from a completed swappable game.
 * Uses plain ints so the SM Manager has no dependency on game-specific enums.
 */
struct SwappableOutcome {
    static constexpr int IN_PROGRESS = 0;
    static constexpr int WON = 1;
    static constexpr int LOST = 2;

    int result = IN_PROGRESS;
    int score = 0;
    bool isComplete() const { return result != IN_PROGRESS; }
};

/*
 * SwappableGame — the contract a game must satisfy to be managed by
 * the StateMachineManager. Extends StateMachine with completion signaling.
 *
 * Concrete games (e.g. MiniGame) implement this interface.
 * The SM Manager never needs to know the concrete type.
 */
class SwappableGame : public StateMachine {
public:
    using StateMachine::StateMachine;
    virtual ~SwappableGame() = default;

    virtual bool isReadyForResume() const = 0;
    virtual SwappableOutcome getOutcome() const = 0;
    virtual int getGameId() const = 0;
};

/*
 * StateMachineManager manages transitions between the default state machine
 * (e.g. Quickdraw) and temporary swappable game state machines.
 *
 * When a swap is triggered, the SM Manager:
 * 1. Pauses the current default state machine (dismounts current state)
 * 2. Loads and initializes the swappable game
 * 3. Runs the game until it signals readyForResume
 * 4. Auto-resumes the default state machine at a specified state index
 *
 * The SM Manager does NOT own the default state machine. It DOES own
 * the swapped game and deletes it on resume.
 */
class StateMachineManager {
public:
    explicit StateMachineManager(Device* device);
    ~StateMachineManager();

    void setDefaultStateMachine(StateMachine* sm);

    /*
     * Pause the default SM and load a swappable game.
     * Takes ownership of 'game' (will delete on resume).
     */
    void pauseAndLoad(SwappableGame* game, int resumeStateIndex);

    /*
     * Resume the default SM after a game completes.
     * Stores outcome, deletes the game, skips default SM to resumeStateIndex.
     */
    void resumePrevious();

    /*
     * Main loop — delegates to whichever SM is active.
     * Auto-calls resumePrevious() when the swapped game signals readyForResume.
     */
    void loop();

    StateMachine* getActive() const;
    bool isSwapped() const;
    SwappableOutcome getLastOutcome() const;
    int getLastGameId() const;

private:
    Device* device;
    StateMachine* defaultSM = nullptr;    // NOT owned
    SwappableGame* swappedSM = nullptr;   // OWNED (deleted on resume)
    bool swapped = false;
    int resumeStateIndex_ = -1;
    SwappableOutcome lastOutcome_;
    int lastGameId_ = 0;
};
