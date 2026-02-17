#pragma once

#ifdef NATIVE_BUILD

#include <gtest/gtest.h>
#include "cli/cli-device.hpp"
#include "cli/cli-serial-broker.hpp"
#include "cli/cli-http-server.hpp"
#include "game/quickdraw.hpp"
#include "game/quickdraw-states.hpp"
#include "game/signal-echo/signal-echo.hpp"
#include "game/signal-echo/signal-echo-states.hpp"
#include "game/ghost-runner/ghost-runner.hpp"
#include "game/ghost-runner/ghost-runner-states.hpp"
#include "game/spike-vector/spike-vector.hpp"
#include "game/spike-vector/spike-vector-states.hpp"
#include "game/cipher-path/cipher-path.hpp"
#include "game/cipher-path/cipher-path-states.hpp"
#include "device/device-types.hpp"
#include "utils/simple-timer.hpp"
#include "game/difficulty-scaler.hpp"
#include <cstdint>

using namespace cli;

// ============================================
// EDGE CASE BOUNDARY TEST SUITE
// ============================================

class EdgeCaseBoundaryTestSuite : public testing::Test {
public:
    void SetUp() override {
        // Reset all singleton state before each test to prevent pollution
        SerialCableBroker::resetInstance();
        MockHttpServer::resetInstance();
        SimpleTimer::resetClock();

        device_ = DeviceFactory::createDevice(0, true);
        SimpleTimer::setPlatformClock(device_.clockDriver);
        player_ = device_.player;
        quickdraw_ = dynamic_cast<Quickdraw*>(device_.game);
    }

    void TearDown() override {
        DeviceFactory::destroyDevice(device_);

        // Clean up singleton state after each test
        SerialCableBroker::resetInstance();
        MockHttpServer::resetInstance();
        SimpleTimer::resetClock();
    }

    void tick(int n = 1) {
        for (int i = 0; i < n; i++) {
            device_.pdn->loop();
        }
    }

    void tickWithTime(int n, int delayMs) {
        for (int i = 0; i < n; i++) {
            device_.clockDriver->advance(delayMs);
            device_.pdn->loop();
        }
    }

    void advanceToIdle() {
        quickdraw_->skipToState(device_.pdn, 6);
        device_.pdn->loop();
    }

    int getStateId() {
        return quickdraw_->getCurrentState()->getStateId();
    }

    DeviceInstance device_;
    Player* player_ = nullptr;
    Quickdraw* quickdraw_ = nullptr;
};

// ============================================
// MINIGAME BOUNDARY TEST SUITE
// ============================================

class MinigameBoundaryTestSuite : public testing::Test {
public:
    void SetUp() override {
        SerialCableBroker::resetInstance();
        MockHttpServer::resetInstance();
        SimpleTimer::resetClock();
    }

    void TearDown() override {
        SerialCableBroker::resetInstance();
        MockHttpServer::resetInstance();
        SimpleTimer::resetClock();
    }

    void tick(DeviceInstance& device, int n = 1) {
        for (int i = 0; i < n; i++) {
            device.pdn->loop();
        }
    }

    void tickWithTime(DeviceInstance& device, int n, int delayMs) {
        for (int i = 0; i < n; i++) {
            device.clockDriver->advance(delayMs);
            device.pdn->loop();
        }
    }
};

// ============================================
// TWO-DEVICE FDN BOUNDARY TEST SUITE
// ============================================

class FdnBoundaryTestSuite : public testing::Test {
public:
    void SetUp() override {
        player_ = DeviceFactory::createDevice(0, true);
        fdn_ = DeviceFactory::createFdnDevice(1, GameType::SIGNAL_ECHO);
        SimpleTimer::setPlatformClock(player_.clockDriver);
        SerialCableBroker::getInstance().connect(0, 1);
    }

    void TearDown() override {
        SerialCableBroker::getInstance().disconnect(0, 1);
        DeviceFactory::destroyDevice(fdn_);
        DeviceFactory::destroyDevice(player_);
    }

    void tick(int n = 1) {
        for (int i = 0; i < n; i++) {
            SerialCableBroker::getInstance().transferData();
            player_.pdn->loop();
            fdn_.pdn->loop();
        }
    }

    void tickWithTime(int n, int delayMs) {
        for (int i = 0; i < n; i++) {
            player_.clockDriver->advance(delayMs);
            fdn_.clockDriver->advance(delayMs);
            SerialCableBroker::getInstance().transferData();
            player_.pdn->loop();
            fdn_.pdn->loop();
        }
    }

    void advanceToIdle() {
        player_.game->skipToState(player_.pdn, 6);
        player_.pdn->loop();
    }

    int getPlayerStateId() {
        return player_.game->getCurrentState()->getStateId();
    }

    DeviceInstance player_;
    DeviceInstance fdn_;
};

// ============================================
// BOUNDARY VALUE TESTS
// ============================================

/*
 * Test: Difficulty scaler at minimum (fresh player)
 * Verifies initial difficulty is at minimum bound
 */
void boundaryDifficultyLevelZero(EdgeCaseBoundaryTestSuite* suite) {
    DifficultyScaler scaler;

    // Fresh scaler should start at minimum difficulty (0.0)
    float difficulty = scaler.getScaledDifficulty(GameType::SIGNAL_ECHO);
    ASSERT_GE(difficulty, 0.0f);
    ASSERT_LE(difficulty, 1.0f);
    ASSERT_FLOAT_EQ(difficulty, 0.0f);
}

/*
 * Test: Difficulty scaler at maximum (after many wins)
 * Verifies difficulty caps at maximum after winning streak
 */
void boundaryDifficultyLevelMax(EdgeCaseBoundaryTestSuite* suite) {
    DifficultyScaler scaler;

    // Record 100 wins to max out difficulty
    for (int i = 0; i < 100; i++) {
        scaler.recordResult(GameType::SIGNAL_ECHO, true, 1000);
    }

    float difficulty = scaler.getScaledDifficulty(GameType::SIGNAL_ECHO);
    ASSERT_GE(difficulty, 0.0f);
    ASSERT_LE(difficulty, 1.0f);

    // Should be at or near maximum
    ASSERT_GE(difficulty, 0.9f);
}

/*
 * Test: Timer with zero duration
 * Verifies zero-duration timers expire immediately
 */
void boundaryTimerZeroDuration(EdgeCaseBoundaryTestSuite* suite) {
    SimpleTimer timer;
    timer.setTimer(0);

    // Zero duration should be expired immediately
    ASSERT_TRUE(timer.expired());
}

/*
 * Test: Timer with 1ms duration
 * Verifies minimal duration timer works correctly
 */
void boundaryTimerOneMillisecond(EdgeCaseBoundaryTestSuite* suite) {
    SimpleTimer timer;
    timer.setTimer(1);

    // Should not be expired immediately
    ASSERT_FALSE(timer.expired());

    // Should be expired after 1ms
    suite->device_.clockDriver->advance(1);
    ASSERT_TRUE(timer.expired());
}

/*
 * Test: Timer with maximum duration
 * Verifies large duration timers work correctly
 */
void boundaryTimerMaxDuration(EdgeCaseBoundaryTestSuite* suite) {
    SimpleTimer timer;
    timer.setTimer(3600000); // 1 hour

    ASSERT_FALSE(timer.expired());

    // Should not expire prematurely
    suite->device_.clockDriver->advance(3599999);
    ASSERT_FALSE(timer.expired());

    // Should expire after full duration
    suite->device_.clockDriver->advance(1);
    ASSERT_TRUE(timer.expired());
}

// ============================================
// RAPID INPUT STRESS TESTS
// ============================================

/*
 * Test: Button spam during state transition
 * Verifies 10 button presses in 100ms during transition
 */
void rapidInputButtonSpamDuringTransition(EdgeCaseBoundaryTestSuite* suite) {
    suite->advanceToIdle();
    ASSERT_EQ(suite->getStateId(), IDLE);

    // Trigger state transition
    suite->device_.serialOutDriver->injectInput("*fdn:7:6\r");

    // Spam button 10 times during transition
    for (int i = 0; i < 10; i++) {
        suite->device_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    }

    suite->tick(5);

    // Should still be in a valid state
    int stateId = suite->getStateId();
    ASSERT_GE(stateId, 0);
    ASSERT_LE(stateId, 26);
}

/*
 * Test: Simultaneous button1 and button2 press
 * Verifies both buttons pressed at exact same moment
 */
void rapidInputSimultaneousButtons(EdgeCaseBoundaryTestSuite* suite) {
    suite->advanceToIdle();

    // Press both buttons simultaneously 50 times
    for (int i = 0; i < 50; i++) {
        suite->device_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
        suite->device_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
        suite->tick(1);
    }

    // Should handle gracefully
    ASSERT_EQ(suite->getStateId(), IDLE);
}

/*
 * Test: Button press during intro animation
 * Verifies button input during state mount phase
 */
void rapidInputButtonDuringIntro(MinigameBoundaryTestSuite* suite) {
    DeviceInstance device = DeviceFactory::createGameDevice(0, "signal-echo");
    SimpleTimer::setPlatformClock(device.clockDriver);
    SignalEcho* echo = static_cast<SignalEcho*>(device.game);

    // Start intro
    echo->skipToState(device.pdn, 0);
    suite->tick(device, 1);

    ASSERT_EQ(echo->getCurrentState()->getStateId(), ECHO_INTRO);

    // Spam buttons during intro animation
    for (int i = 0; i < 20; i++) {
        device.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
        device.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
        suite->tick(device, 1);
    }

    // Should still transition correctly after timer expires
    suite->tickWithTime(device, 5, 500);
    suite->tick(device, 1);

    // Should be in next state
    int stateId = echo->getCurrentState()->getStateId();
    ASSERT_NE(stateId, ECHO_INTRO);

    DeviceFactory::destroyDevice(device);
}

/*
 * Test: Button press during win/lose display
 * Verifies button input during outcome states
 */
void rapidInputButtonDuringOutcome(MinigameBoundaryTestSuite* suite) {
    DeviceInstance device = DeviceFactory::createGameDevice(0, "signal-echo");
    SimpleTimer::setPlatformClock(device.clockDriver);
    SignalEcho* echo = static_cast<SignalEcho*>(device.game);

    // Jump to lose state
    echo->skipToState(device.pdn, 5);
    suite->tick(device, 1);

    ASSERT_EQ(echo->getCurrentState()->getStateId(), ECHO_LOSE);

    // Spam buttons during outcome display
    for (int i = 0; i < 30; i++) {
        device.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
        device.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
        suite->tick(device, 1);
    }

    // Should handle gracefully
    ASSERT_NE(echo->getCurrentState(), nullptr);

    DeviceFactory::destroyDevice(device);
}

/*
 * Test: Alternating rapid button presses
 * Verifies handling of 100 alternating button presses
 */
void rapidInputAlternatingButtons(EdgeCaseBoundaryTestSuite* suite) {
    suite->advanceToIdle();

    // Alternate buttons 100 times
    for (int i = 0; i < 100; i++) {
        if (i % 2 == 0) {
            suite->device_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
        } else {
            suite->device_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
        }
    }

    suite->tick(5);
    ASSERT_EQ(suite->getStateId(), IDLE);
}

// ============================================
// INTERRUPTED FLOW TESTS
// ============================================

/*
 * Test: Cable disconnect during game evaluation
 * Verifies graceful handling of disconnect mid-evaluation
 */
void interruptedFlowCableDisconnectDuringEval(FdnBoundaryTestSuite* suite) {
    suite->advanceToIdle();

    // Start FDN interaction
    suite->player_.serialOutDriver->injectInput("*fdn:7:6\r");
    suite->tick(3);
    ASSERT_EQ(suite->getPlayerStateId(), FDN_DETECTED);

    // Complete handshake
    suite->player_.serialOutDriver->injectInput("*fack\r");
    suite->tickWithTime(5, 100);

    // Disconnect during game flow
    SerialCableBroker::getInstance().disconnect(0, 1);
    suite->tick(10);

    // Should handle disconnect gracefully
    int stateId = suite->getPlayerStateId();
    ASSERT_GE(stateId, 0);
    ASSERT_LE(stateId, 26);
}

/*
 * Test: Multiple rapid cable connect/disconnect
 * Verifies handling of 10 rapid connect/disconnect cycles
 */
void interruptedFlowRapidCableToggle(FdnBoundaryTestSuite* suite) {
    suite->advanceToIdle();

    // Perform 10 rapid connect/disconnect cycles
    for (int i = 0; i < 10; i++) {
        SerialCableBroker::getInstance().disconnect(0, 1);
        suite->tick(2);

        SerialCableBroker::getInstance().connect(0, 1);
        suite->tick(2);
    }

    // Should still be functional
    int stateId = suite->getPlayerStateId();
    ASSERT_GE(stateId, 0);
    ASSERT_LE(stateId, 26);
}

/*
 * Test: Game state when no NPC connected
 * Verifies idle state behavior with no FDN present
 */
void interruptedFlowNoNpcConnected(EdgeCaseBoundaryTestSuite* suite) {
    suite->advanceToIdle();
    ASSERT_EQ(suite->getStateId(), IDLE);

    // Run for extended period with no NPC
    suite->tickWithTime(100, 100);

    // Should remain in idle state
    ASSERT_EQ(suite->getStateId(), IDLE);
}

/*
 * Test: NPC disconnect immediately after handshake
 * Verifies handling of disconnect right after fack
 */
void interruptedFlowDisconnectAfterHandshake(FdnBoundaryTestSuite* suite) {
    suite->advanceToIdle();

    suite->player_.serialOutDriver->injectInput("*fdn:7:6\r");
    suite->tick(3);
    ASSERT_EQ(suite->getPlayerStateId(), FDN_DETECTED);

    suite->player_.serialOutDriver->injectInput("*fack\r");
    suite->tick(1);

    // Disconnect immediately after handshake
    SerialCableBroker::getInstance().disconnect(0, 1);
    suite->tick(10);

    // Should handle gracefully
    int stateId = suite->getPlayerStateId();
    ASSERT_GE(stateId, 0);
}

/*
 * Test: Button press during cable disconnect
 * Verifies combined button input and disconnect event
 */
void interruptedFlowButtonDuringDisconnect(FdnBoundaryTestSuite* suite) {
    suite->advanceToIdle();

    suite->player_.serialOutDriver->injectInput("*fdn:7:6\r");
    suite->tick(3);

    // Press button while disconnecting
    suite->player_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    SerialCableBroker::getInstance().disconnect(0, 1);
    suite->player_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);

    suite->tick(5);

    // Should handle concurrent events
    int stateId = suite->getPlayerStateId();
    ASSERT_GE(stateId, 0);
}

// ============================================
// DIFFICULTY SCALER EDGE CASES
// ============================================

/*
 * Test: Difficulty after 100+ games played
 * Verifies difficulty scaling with extensive play history
 */
void difficultyScalerAfterManyGames(EdgeCaseBoundaryTestSuite* suite) {
    DifficultyScaler scaler;

    // Simulate 50 wins, 50 losses (balanced performance)
    for (int i = 0; i < 50; i++) {
        scaler.recordResult(GameType::SIGNAL_ECHO, true, 2000);
        scaler.recordResult(GameType::SIGNAL_ECHO, false, 5000);
    }

    float difficulty = scaler.getScaledDifficulty(GameType::SIGNAL_ECHO);

    // Should be in valid range
    ASSERT_GE(difficulty, 0.0f);
    ASSERT_LE(difficulty, 1.0f);

    // With alternating win/loss, the SCALE_DOWN_STEP (0.08) is larger than
    // SCALE_UP_STEP (0.05), so difficulty trends downward toward 0.
    // Net effect per pair: +0.05 - 0.08 = -0.03, so after 50 pairs difficulty ≈ 0.0
    ASSERT_FLOAT_EQ(difficulty, 0.0f);
}

/*
 * Test: Difficulty for each game type at level 0
 * Verifies all games have valid minimum difficulty
 */
void difficultyScalerAllGamesLevelZero(EdgeCaseBoundaryTestSuite* suite) {
    DifficultyScaler scaler;

    GameType games[] = {
        GameType::SIGNAL_ECHO,
        GameType::GHOST_RUNNER,
        GameType::SPIKE_VECTOR,
        GameType::CIPHER_PATH,
        GameType::EXPLOIT_SEQUENCER,
        GameType::BREACH_DEFENSE
    };

    for (GameType game : games) {
        float difficulty = scaler.getScaledDifficulty(game);
        ASSERT_GE(difficulty, 0.0f);
        ASSERT_LE(difficulty, 1.0f);
        ASSERT_FLOAT_EQ(difficulty, 0.0f); // Should start at 0
    }
}

/*
 * Test: Difficulty for each game type after wins
 * Verifies all games scale up with wins
 */
void difficultyScalerAllGamesMaxLevel(EdgeCaseBoundaryTestSuite* suite) {
    DifficultyScaler scaler;

    GameType games[] = {
        GameType::SIGNAL_ECHO,
        GameType::GHOST_RUNNER,
        GameType::SPIKE_VECTOR,
        GameType::CIPHER_PATH,
        GameType::EXPLOIT_SEQUENCER,
        GameType::BREACH_DEFENSE
    };

    for (GameType game : games) {
        // Record several wins
        for (int i = 0; i < 20; i++) {
            scaler.recordResult(game, true, 1000);
        }

        float difficulty = scaler.getScaledDifficulty(game);
        ASSERT_GE(difficulty, 0.0f);
        ASSERT_LE(difficulty, 1.0f);

        // Should have increased from 0
        ASSERT_GT(difficulty, 0.0f);
    }
}

/*
 * Test: Difficulty progression is monotonic (increases with wins)
 * Verifies difficulty increases with consecutive wins
 */
void difficultyScalerMonotonicProgression(EdgeCaseBoundaryTestSuite* suite) {
    DifficultyScaler scaler;

    float prevDifficulty = scaler.getScaledDifficulty(GameType::SIGNAL_ECHO);

    // Record 20 consecutive wins
    for (int i = 0; i < 20; i++) {
        scaler.recordResult(GameType::SIGNAL_ECHO, true, 1000);

        float difficulty = scaler.getScaledDifficulty(GameType::SIGNAL_ECHO);
        ASSERT_GE(difficulty, prevDifficulty) << "Difficulty decreased after win " << i;
        prevDifficulty = difficulty;
    }
}

// ============================================
// SERIAL PROTOCOL EDGE CASES
// ============================================

/*
 * Test: Empty serial message
 * Verifies handling of empty string
 */
void serialProtocolEmptyMessage(EdgeCaseBoundaryTestSuite* suite) {
    suite->advanceToIdle();

    suite->device_.serialOutDriver->injectInput("\r");
    suite->tick(5);

    // Should ignore gracefully
    ASSERT_EQ(suite->getStateId(), IDLE);
}

/*
 * Test: Very long serial message
 * Verifies handling of message exceeding buffer size
 */
void serialProtocolLongMessage(EdgeCaseBoundaryTestSuite* suite) {
    suite->advanceToIdle();

    // Create very long message (500 characters)
    std::string longMsg = "*fdn:7:6:";
    for (int i = 0; i < 500; i++) {
        longMsg += "x";
    }
    longMsg += "\r";

    suite->device_.serialOutDriver->injectInput(longMsg);
    suite->tick(5);

    // Should handle without crash
    int stateId = suite->getStateId();
    ASSERT_GE(stateId, 0);
}

/*
 * Test: Malformed FDN message with missing fields
 * Verifies error handling for incomplete protocol
 */
void serialProtocolMalformedFdn(EdgeCaseBoundaryTestSuite* suite) {
    suite->advanceToIdle();

    // Send malformed FDN (missing game type)
    suite->device_.serialOutDriver->injectInput("*fdn:\r");
    suite->tick(5);

    // Should ignore or handle gracefully
    int stateId = suite->getStateId();
    ASSERT_GE(stateId, 0);
}

/*
 * Test: Serial message flood (100 messages)
 * Verifies buffer handling with rapid message injection
 */
void serialProtocolMessageFlood(EdgeCaseBoundaryTestSuite* suite) {
    suite->advanceToIdle();

    // Flood with 100 rapid messages
    for (int i = 0; i < 100; i++) {
        suite->device_.serialOutDriver->injectInput("*fdn:7:6\r");
    }

    suite->tick(10);

    // Should handle without overflow
    ASSERT_NE(suite->device_.pdn, nullptr);
}

/*
 * Test: Null character in message
 * Verifies handling of embedded null bytes
 */
void serialProtocolNullCharacter(EdgeCaseBoundaryTestSuite* suite) {
    suite->advanceToIdle();

    std::string msgWithNull = "*fdn";
    msgWithNull += '\0';
    msgWithNull += ":7:6\r";

    suite->device_.serialOutDriver->injectInput(msgWithNull);
    suite->tick(5);

    // Should handle gracefully
    int stateId = suite->getStateId();
    ASSERT_GE(stateId, 0);
}

#endif // NATIVE_BUILD
