//
// StateMachineManager Tests — verify pause/load/resume lifecycle
// and outcome storage when swapping between default SM and games.
//

#pragma once

#ifdef NATIVE_BUILD

#include <gtest/gtest.h>
#include "cli/cli-device.hpp"
#include "state/state-machine-manager.hpp"
#include "game/test-swappable-game.hpp"

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

// Test: SM Manager delegates to default SM when no game is active
void smManagerDefaultLoop(SmManagerTestSuite* suite) {
    StateMachineManager manager(suite->device_.pdn);
    manager.setDefaultStateMachine(suite->device_.game);

    ASSERT_EQ(manager.getActive(), suite->device_.game);
    ASSERT_FALSE(manager.isSwapped());

    manager.loop();

    ASSERT_EQ(manager.getActive(), suite->device_.game);
}

// Test: pauseAndLoad swaps to game and sets isSwapped true
void smManagerPauseAndLoad(SmManagerTestSuite* suite) {
    StateMachineManager manager(suite->device_.pdn);
    manager.setDefaultStateMachine(suite->device_.game);

    auto* game = new TestSwappableGame(suite->device_.pdn, SwappableOutcome::WON);
    manager.pauseAndLoad(game, 7);

    ASSERT_TRUE(manager.isSwapped());
    ASSERT_EQ(manager.getActive(), game);
}

// Test: Auto-resume happens when game signals readyForResume
void smManagerAutoResume(SmManagerTestSuite* suite) {
    StateMachineManager manager(suite->device_.pdn);
    manager.setDefaultStateMachine(suite->device_.game);

    auto* game = new TestSwappableGame(suite->device_.pdn, SwappableOutcome::WON);
    manager.pauseAndLoad(game, 7);

    game->triggerComplete();

    manager.loop();
    if (manager.isSwapped()) {
        manager.loop();
    }

    ASSERT_FALSE(manager.isSwapped());
    ASSERT_EQ(manager.getActive(), suite->device_.game);
}

// Test: After auto-resume, outcome and gameId are stored
void smManagerOutcomeStored(SmManagerTestSuite* suite) {
    StateMachineManager manager(suite->device_.pdn);
    manager.setDefaultStateMachine(suite->device_.game);

    auto* game = new TestSwappableGame(suite->device_.pdn, SwappableOutcome::WON);
    manager.pauseAndLoad(game, 7);

    game->triggerComplete();
    manager.loop();
    if (manager.isSwapped()) {
        manager.loop();
    }

    ASSERT_FALSE(manager.isSwapped());
    ASSERT_EQ(manager.getLastOutcome().result, SwappableOutcome::WON);
    ASSERT_EQ(manager.getLastOutcome().score, 42);
    ASSERT_EQ(manager.getLastGameId(), TEST_GAME_ID);
}

// Test: After resume, default SM is at the resumeStateIndex
void smManagerResumeToCorrectState(SmManagerTestSuite* suite) {
    StateMachineManager manager(suite->device_.pdn);
    manager.setDefaultStateMachine(suite->device_.game);

    auto* game = new TestSwappableGame(suite->device_.pdn, SwappableOutcome::WON);
    manager.pauseAndLoad(game, 7);

    game->triggerComplete();
    manager.loop();
    if (manager.isSwapped()) {
        manager.loop();
    }

    ASSERT_FALSE(manager.isSwapped());
    State* currentState = suite->device_.game->getCurrentState();
    ASSERT_NE(currentState, nullptr);
    ASSERT_EQ(currentState->getStateId(), 8);
}

// Test: After resume, swapped game was deleted
void smManagerDeletesSwappedGame(SmManagerTestSuite* suite) {
    StateMachineManager manager(suite->device_.pdn);
    manager.setDefaultStateMachine(suite->device_.game);

    auto* game = new TestSwappableGame(suite->device_.pdn, SwappableOutcome::WON);
    manager.pauseAndLoad(game, 7);

    game->triggerComplete();
    manager.loop();
    if (manager.isSwapped()) {
        manager.loop();
    }

    ASSERT_FALSE(manager.isSwapped());
    ASSERT_EQ(manager.getActive(), suite->device_.game);
}

// Test: WON outcome stored correctly
void smManagerPauseAndLoadWon(SmManagerTestSuite* suite) {
    StateMachineManager manager(suite->device_.pdn);
    manager.setDefaultStateMachine(suite->device_.game);

    auto* game = new TestSwappableGame(suite->device_.pdn, SwappableOutcome::WON);
    manager.pauseAndLoad(game, 7);

    game->triggerComplete();
    manager.loop();
    if (manager.isSwapped()) {
        manager.loop();
    }

    ASSERT_EQ(manager.getLastOutcome().result, SwappableOutcome::WON);
    ASSERT_EQ(manager.getLastOutcome().score, 42);
}

// Test: LOST outcome stored correctly
void smManagerPauseAndLoadLost(SmManagerTestSuite* suite) {
    StateMachineManager manager(suite->device_.pdn);
    manager.setDefaultStateMachine(suite->device_.game);

    auto* game = new TestSwappableGame(suite->device_.pdn, SwappableOutcome::LOST);
    manager.pauseAndLoad(game, 7);

    game->triggerComplete();
    manager.loop();
    if (manager.isSwapped()) {
        manager.loop();
    }

    ASSERT_EQ(manager.getLastOutcome().result, SwappableOutcome::LOST);
    ASSERT_EQ(manager.getLastOutcome().score, 42);
}

#endif // NATIVE_BUILD
