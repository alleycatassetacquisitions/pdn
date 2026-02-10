//
// StateMachineManager Tests — verify pause/load/resume lifecycle
// and outcome storage when swapping between Quickdraw and minigames.
//

#pragma once

#ifdef NATIVE_BUILD

#include <gtest/gtest.h>
#include "cli/cli-device.hpp"
#include "state/state-machine-manager.hpp"
#include "game/test-minigame.hpp"

// ============================================
// SM MANAGER TEST SUITE
// ============================================

class SmManagerTestSuite : public testing::Test {
public:
    cli::DeviceInstance device_;

    void SetUp() override {
        device_ = cli::DeviceFactory::createDevice(0, true);
        // Run a few loops to get past initial states
        for (int i = 0; i < 5; i++) {
            device_.pdn->loop();
            device_.game->loop();
        }
        // Skip to Idle (stateMap index 7)
        device_.game->skipToState(7);
    }

    void TearDown() override {
        cli::DeviceFactory::destroyDevice(device_);
    }
};

// Test: SM Manager delegates to default SM when no minigame is active
void smManagerDefaultLoop(SmManagerTestSuite* suite) {
    StateMachineManager manager(suite->device_.pdn);
    manager.setDefaultStateMachine(suite->device_.game);

    // Verify default SM is active
    ASSERT_EQ(manager.getActive(), suite->device_.game);
    ASSERT_FALSE(manager.isSwapped());

    // Loop should delegate to default SM without crashing
    manager.loop();

    // Still on default SM
    ASSERT_EQ(manager.getActive(), suite->device_.game);
}

// Test: pauseAndLoad swaps to minigame and sets isSwapped true
void smManagerPauseAndLoad(SmManagerTestSuite* suite) {
    StateMachineManager manager(suite->device_.pdn);
    manager.setDefaultStateMachine(suite->device_.game);

    auto* miniGame = new TestMiniGame(suite->device_.pdn, MiniGameResult::WON);
    manager.pauseAndLoad(miniGame, 7);

    ASSERT_TRUE(manager.isSwapped());
    ASSERT_EQ(manager.getActive(), miniGame);
}

// Test: Auto-resume happens when minigame signals readyForResume
void smManagerAutoResume(SmManagerTestSuite* suite) {
    StateMachineManager manager(suite->device_.pdn);
    manager.setDefaultStateMachine(suite->device_.game);

    auto* miniGame = new TestMiniGame(suite->device_.pdn, MiniGameResult::WON);
    manager.pauseAndLoad(miniGame, 7);

    // Trigger the minigame to complete
    miniGame->triggerComplete();

    // One loop: transitions from Intro to Complete (sets readyForResume)
    manager.loop();
    // Another loop: the SM Manager detects readyForResume and auto-resumes
    // (may happen in the same loop if the transition+mount+readyForResume
    //  all fire in one cycle — but let's be safe and loop again)
    if (manager.isSwapped()) {
        manager.loop();
    }

    ASSERT_FALSE(manager.isSwapped());
    ASSERT_EQ(manager.getActive(), suite->device_.game);
}

// Test: After auto-resume, outcome and gameType are stored
void smManagerOutcomeStored(SmManagerTestSuite* suite) {
    StateMachineManager manager(suite->device_.pdn);
    manager.setDefaultStateMachine(suite->device_.game);

    auto* miniGame = new TestMiniGame(suite->device_.pdn, MiniGameResult::WON);
    manager.pauseAndLoad(miniGame, 7);

    miniGame->triggerComplete();
    manager.loop();
    if (manager.isSwapped()) {
        manager.loop();
    }

    ASSERT_FALSE(manager.isSwapped());
    ASSERT_EQ(manager.getLastOutcome().result, MiniGameResult::WON);
    ASSERT_EQ(manager.getLastOutcome().score, 42);
    ASSERT_EQ(manager.getLastGameType(), GameType::SIGNAL_ECHO);
}

// Test: After resume, default SM is at the resumeStateIndex
void smManagerResumeToCorrectState(SmManagerTestSuite* suite) {
    StateMachineManager manager(suite->device_.pdn);
    manager.setDefaultStateMachine(suite->device_.game);

    // Resume to stateMap index 7 (Idle, stateId = 8)
    auto* miniGame = new TestMiniGame(suite->device_.pdn, MiniGameResult::WON);
    manager.pauseAndLoad(miniGame, 7);

    miniGame->triggerComplete();
    manager.loop();
    if (manager.isSwapped()) {
        manager.loop();
    }

    ASSERT_FALSE(manager.isSwapped());
    // Verify we're back at Idle (stateId 8 based on the Quickdraw state map)
    State* currentState = suite->device_.game->getCurrentState();
    ASSERT_NE(currentState, nullptr);
    ASSERT_EQ(currentState->getStateId(), 8);
}

// Test: After resume, swapped flag is false (game was deleted)
void smManagerDeletesSwappedGame(SmManagerTestSuite* suite) {
    StateMachineManager manager(suite->device_.pdn);
    manager.setDefaultStateMachine(suite->device_.game);

    auto* miniGame = new TestMiniGame(suite->device_.pdn, MiniGameResult::WON);
    manager.pauseAndLoad(miniGame, 7);

    miniGame->triggerComplete();
    manager.loop();
    if (manager.isSwapped()) {
        manager.loop();
    }

    // The swapped game should be deleted — verify via isSwapped() == false
    ASSERT_FALSE(manager.isSwapped());
    // getActive() should return the default SM, not a dangling pointer
    ASSERT_EQ(manager.getActive(), suite->device_.game);
}

// Test: Pause with WON result, verify outcome after resume
void smManagerPauseAndLoadWon(SmManagerTestSuite* suite) {
    StateMachineManager manager(suite->device_.pdn);
    manager.setDefaultStateMachine(suite->device_.game);

    auto* miniGame = new TestMiniGame(suite->device_.pdn, MiniGameResult::WON);
    manager.pauseAndLoad(miniGame, 7);

    miniGame->triggerComplete();
    manager.loop();
    if (manager.isSwapped()) {
        manager.loop();
    }

    ASSERT_EQ(manager.getLastOutcome().result, MiniGameResult::WON);
    ASSERT_EQ(manager.getLastOutcome().score, 42);
}

// Test: Pause with LOST result, verify outcome after resume
void smManagerPauseAndLoadLost(SmManagerTestSuite* suite) {
    StateMachineManager manager(suite->device_.pdn);
    manager.setDefaultStateMachine(suite->device_.game);

    auto* miniGame = new TestMiniGame(suite->device_.pdn, MiniGameResult::LOST);
    manager.pauseAndLoad(miniGame, 7);

    miniGame->triggerComplete();
    manager.loop();
    if (manager.isSwapped()) {
        manager.loop();
    }

    ASSERT_EQ(manager.getLastOutcome().result, MiniGameResult::LOST);
    ASSERT_EQ(manager.getLastOutcome().score, 42);
}

#endif // NATIVE_BUILD
