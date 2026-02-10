#pragma once

#include <cstdint>
#include "state/state-machine.hpp"
#include "game/minigame.hpp"
#include "device/device-types.hpp"

/*
 * StateMachineManager manages transitions between the default state machine
 * (e.g. Quickdraw) and temporary minigame state machines.
 *
 * When a player interacts with a Challenge Device NPC, the SM Manager:
 * 1. Pauses the current Quickdraw state machine (dismounts current state)
 * 2. Loads and initializes the minigame
 * 3. Runs the minigame until completion
 * 4. Auto-resumes the previous state machine at a specified state index
 *
 * The SM Manager does NOT own the default state machine. It DOES own
 * the swapped (minigame) state machine and deletes it on resume.
 */
class StateMachineManager {
public:
    explicit StateMachineManager(Device* device);
    ~StateMachineManager();

    /*
     * Set the default (primary) state machine that runs when no minigame is active.
     * The SM Manager does NOT take ownership — caller is responsible for deletion.
     */
    void setDefaultStateMachine(StateMachine* sm);

    /*
     * Pause the default SM and load a minigame.
     * - Dismounts the current state on the default SM
     * - Stores the resumeStateIndex for later
     * - Takes ownership of 'game' (will delete on resume)
     * - Initializes and starts the minigame
     */
    void pauseAndLoad(MiniGame* game, int resumeStateIndex);

    /*
     * Resume the default SM after a minigame completes.
     * - Stores outcome + gameType from the completed game
     * - Dismounts the minigame's current state
     * - Deletes the minigame
     * - Skips the default SM to the stored resumeStateIndex
     */
    void resumePrevious();

    /*
     * Main loop — delegates to whichever SM is currently active.
     * If a swapped minigame signals readyForResume, auto-calls resumePrevious().
     */
    void loop();

    StateMachine* getActive() const;
    bool isSwapped() const;
    const MiniGameOutcome& getLastOutcome() const;
    GameType getLastGameType() const;

private:
    Device* device;
    StateMachine* defaultSM = nullptr;   // NOT owned
    MiniGame* swappedSM = nullptr;       // OWNED (deleted on resume)
    bool swapped = false;
    int resumeStateIndex_ = -1;
    MiniGameOutcome lastOutcome;
    GameType lastGameType = GameType::QUICKDRAW;
};
