#pragma once

#ifdef NATIVE_BUILD

#include <gtest/gtest.h>
#include "cli/cli-device-full.hpp"
#include "game/signal-echo/signal-echo.hpp"
#include "game/signal-echo/signal-echo-states.hpp"
#include "game/signal-echo/signal-echo-resources.hpp"
#include "utils/simple-timer.hpp"

using namespace cli;

// ============================================
// SIGNAL ECHO TEST SUITE
// ============================================

class SignalEchoTestSuite : public testing::Test {
public:
    void SetUp() override {
        device_ = DeviceFactory::createGameDevice(0, "signal-echo");
        SimpleTimer::setPlatformClock(device_.clockDriver);
        game_ = dynamic_cast<SignalEcho*>(device_.game);
    }

    void TearDown() override {
        DeviceFactory::destroyDevice(device_);
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

    DeviceInstance device_;
    SignalEcho* game_ = nullptr;
};

// ============================================
// SEQUENCE GENERATION TESTS
// ============================================

// Test: Generated sequence has correct length
void echoSequenceGenerationLength(SignalEchoTestSuite* suite) {
    SignalEchoConfig config;
    config.sequenceLength = 6;
    config.rngSeed = 42;

    SignalEcho tempGame(config);
    tempGame.seedRng(tempGame.getConfig().rngSeed);
    std::vector<bool> seq = tempGame.generateSequence(6);
    ASSERT_EQ(static_cast<int>(seq.size()), 6);

    std::vector<bool> seq2 = tempGame.generateSequence(10);
    ASSERT_EQ(static_cast<int>(seq2.size()), 10);
}

// Test: Sequence contains both UP and DOWN (statistical)
void echoSequenceGenerationMixed(SignalEchoTestSuite* suite) {
    SignalEchoConfig config;
    config.rngSeed = 42;

    SignalEcho tempGame(config);
    tempGame.seedRng(tempGame.getConfig().rngSeed);

    std::vector<bool> seq = tempGame.generateSequence(100);
    int ups = 0, downs = 0;
    for (bool val : seq) {
        if (val) ups++;
        else downs++;
    }
    ASSERT_GT(ups, 0) << "Expected at least one UP in 100 elements";
    ASSERT_GT(downs, 0) << "Expected at least one DOWN in 100 elements";
}

// ============================================
// STATE TRANSITION TESTS
// ============================================

// Test: EchoIntro transitions to EchoShowSequence after timer
void echoIntroTransitionsToShow(SignalEchoTestSuite* suite) {
    State* state = suite->game_->getCurrentState();
    ASSERT_NE(state, nullptr);
    ASSERT_EQ(state->getStateId(), ECHO_INTRO);

    // Advance past 2s intro timer
    suite->tickWithTime(30, 100);

    state = suite->game_->getCurrentState();
    ASSERT_EQ(state->getStateId(), ECHO_SHOW_SEQUENCE);
}

// Test: ShowSequence displays signals
void echoShowSequenceDisplaysSignals(SignalEchoTestSuite* suite) {
    suite->game_->skipToState(suite->device_.pdn, 1);

    State* state = suite->game_->getCurrentState();
    ASSERT_EQ(state->getStateId(), ECHO_SHOW_SEQUENCE);

    // Run a few loops — should still be showing
    suite->tick(2);
    state = suite->game_->getCurrentState();
    ASSERT_EQ(state->getStateId(), ECHO_SHOW_SEQUENCE);
}

// Test: ShowSequence transitions to PlayerInput after all signals
void echoShowTransitionsToInput(SignalEchoTestSuite* suite) {
    suite->game_->getConfig().displaySpeedMs = 50;
    suite->game_->getSession().currentSequence = {true, false};

    suite->game_->skipToState(suite->device_.pdn, 1);
    suite->tick(1);

    // Advance enough time for both signals to display (2 * displaySpeedMs + margin)
    suite->tickWithTime(50, 60);

    State* state = suite->game_->getCurrentState();
    ASSERT_EQ(state->getStateId(), ECHO_PLAYER_INPUT);
}

// ============================================
// PLAYER INPUT TESTS
// ============================================

// Test: Correct button press advances inputIndex
void echoCorrectInputAdvancesIndex(SignalEchoTestSuite* suite) {
    suite->game_->getConfig().displaySpeedMs = 10;
    suite->game_->getSession().currentSequence = {true, false, true, false};
    suite->game_->getSession().inputIndex = 0;

    suite->game_->skipToState(suite->device_.pdn, 2);
    suite->tick(1);

    // Sequence: UP, DOWN, UP, DOWN. Press UP
    suite->device_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tick(1);

    ASSERT_EQ(suite->game_->getSession().inputIndex, 1);
    ASSERT_EQ(static_cast<int>(suite->game_->getSession().playerInput.size()), 1);
    ASSERT_EQ(suite->game_->getSession().playerInput[0], true);
}

// Test: Wrong button press stores input (deferred evaluation)
void echoWrongInputStored(SignalEchoTestSuite* suite) {
    suite->game_->getConfig().displaySpeedMs = 10;
    suite->game_->getSession().currentSequence = {true, false, true, false};
    suite->game_->getSession().inputIndex = 0;

    suite->game_->skipToState(suite->device_.pdn, 2);
    suite->tick(1);

    // Sequence starts with UP, press DOWN — wrong (but no immediate feedback)
    suite->device_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tick(1);

    ASSERT_EQ(suite->game_->getSession().inputIndex, 1);
    ASSERT_EQ(static_cast<int>(suite->game_->getSession().playerInput.size()), 1);
    ASSERT_EQ(suite->game_->getSession().playerInput[0], false);
}

// Test: Completing all inputs correctly leads to next round via Evaluate
void echoAllCorrectInputsNextRound(SignalEchoTestSuite* suite) {
    suite->game_->getConfig().displaySpeedMs = 10;
    suite->game_->getConfig().numSequences = 3;
    suite->game_->getConfig().sequenceLength = 2;
    suite->game_->getConfig().allowedMistakes = 0;
    suite->game_->getSession().currentSequence = {true, false};
    suite->game_->getSession().currentRound = 0;
    suite->game_->getSession().inputIndex = 0;
    suite->game_->getSession().playerInput.clear();

    suite->game_->skipToState(suite->device_.pdn, 2);
    suite->tick(1);

    // Press correct buttons: UP then DOWN
    suite->device_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tick(1);
    suite->device_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tick(4);

    State* state = suite->game_->getCurrentState();
    ASSERT_TRUE(state->getStateId() == ECHO_EVALUATE ||
                state->getStateId() == ECHO_SHOW_SEQUENCE);
}

// Test: Wrong sequence leads to EchoLose (deferred evaluation)
void echoWrongSequenceLoses(SignalEchoTestSuite* suite) {
    suite->game_->getConfig().displaySpeedMs = 10;
    suite->game_->getConfig().allowedMistakes = 0;
    suite->game_->getConfig().sequenceLength = 4;
    suite->game_->getSession().currentSequence = {true, true, true, true};
    suite->game_->getSession().inputIndex = 0;
    suite->game_->getSession().playerInput.clear();

    suite->game_->skipToState(suite->device_.pdn, 2);
    suite->tick(1);

    // Enter wrong sequence: DOWN, DOWN, DOWN, DOWN (correct is all UP)
    for (int i = 0; i < 4; i++) {
        suite->device_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
        suite->tick(1);
    }
    suite->tick(5);

    State* state = suite->game_->getCurrentState();
    ASSERT_EQ(state->getStateId(), ECHO_LOSE);
}

// Test: All rounds completed successfully leads to EchoWin
void echoAllRoundsCompletedWin(SignalEchoTestSuite* suite) {
    suite->game_->getConfig().displaySpeedMs = 10;
    suite->game_->getConfig().numSequences = 1;
    suite->game_->getConfig().sequenceLength = 2;
    suite->game_->getConfig().allowedMistakes = 0;
    suite->game_->getSession().currentSequence = {true, false};
    suite->game_->getSession().currentRound = 0;
    suite->game_->getSession().inputIndex = 0;
    suite->game_->getSession().playerInput.clear();

    suite->game_->skipToState(suite->device_.pdn, 2);
    suite->tick(1);

    // Enter correct sequence
    suite->device_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tick(1);
    suite->device_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tick(5);

    State* state = suite->game_->getCurrentState();
    ASSERT_EQ(state->getStateId(), ECHO_WIN);
}

// ============================================
// CUMULATIVE & FRESH MODE TESTS
// ============================================

// Test: Cumulative mode appends one element each round
void echoCumulativeModeAppends(SignalEchoTestSuite* suite) {
    SignalEchoConfig config;
    config.sequenceLength = 3;
    config.numSequences = 3;
    config.cumulative = true;
    config.rngSeed = 42;
    config.displaySpeedMs = 10;
    config.allowedMistakes = 0;

    SignalEcho* customGame = new SignalEcho(config);
    customGame->initialize(suite->device_.pdn);

    // First round sequence should be length 3
    int firstLen = static_cast<int>(customGame->getSession().currentSequence.size());
    ASSERT_EQ(firstLen, 3);

    // Simulate completing a round with correct input
    auto& session = customGame->getSession();
    session.playerInput = session.currentSequence;  // correct input
    session.inputIndex = firstLen;
    session.currentRound = 0;

    // Trigger evaluate logic
    customGame->skipToState(suite->device_.pdn, 3);  // Evaluate
    suite->device_.pdn->loop();

    ASSERT_EQ(session.currentRound, 1);
    ASSERT_EQ(static_cast<int>(session.currentSequence.size()), 4);

    delete customGame;
}

// Test: Fresh mode generates new sequence each round (same length each round)
void echoFreshModeNewSequence(SignalEchoTestSuite* suite) {
    SignalEchoConfig config;
    config.sequenceLength = 4;
    config.numSequences = 3;
    config.cumulative = false;
    config.rngSeed = 42;
    config.displaySpeedMs = 10;
    config.allowedMistakes = 0;

    SignalEcho* customGame = new SignalEcho(config);
    customGame->initialize(suite->device_.pdn);

    auto& session = customGame->getSession();

    // First round: length should be 4
    ASSERT_EQ(static_cast<int>(session.currentSequence.size()), 4);

    // Simulate completing round 0
    session.inputIndex = 4;
    session.playerInput = session.currentSequence;  // correct input
    session.currentRound = 0;

    customGame->skipToState(suite->device_.pdn, 3);  // Evaluate
    suite->device_.pdn->loop();

    ASSERT_EQ(session.currentRound, 1);
    // Fresh mode: same length each round
    ASSERT_EQ(static_cast<int>(session.currentSequence.size()), 4);

    delete customGame;
}

// ============================================
// OUTCOME & GAME COMPLETE TESTS
// ============================================

// Test: EchoWin sets outcome to WON with score
void echoWinSetsOutcome(SignalEchoTestSuite* suite) {
    suite->game_->getSession().score = 400;
    suite->game_->skipToState(suite->device_.pdn, 4);

    const MiniGameOutcome& outcome = suite->game_->getOutcome();
    ASSERT_EQ(outcome.result, MiniGameResult::WON);
    ASSERT_EQ(outcome.score, 400);
}

// Test: EchoLose sets outcome to LOST
void echoLoseSetsOutcome(SignalEchoTestSuite* suite) {
    suite->game_->getSession().score = 200;
    suite->game_->skipToState(suite->device_.pdn, 5);

    const MiniGameOutcome& outcome = suite->game_->getOutcome();
    ASSERT_EQ(outcome.result, MiniGameResult::LOST);
    ASSERT_EQ(outcome.score, 200);
}

// Test: isGameComplete returns true after win
void echoIsGameCompleteAfterWin(SignalEchoTestSuite* suite) {
    ASSERT_FALSE(suite->game_->isGameComplete());

    suite->game_->skipToState(suite->device_.pdn, 4);
    ASSERT_TRUE(suite->game_->isGameComplete());
}

// Test: resetGame clears outcome back to IN_PROGRESS
void echoResetGameClearsOutcome(SignalEchoTestSuite* suite) {
    suite->game_->skipToState(suite->device_.pdn, 4);
    ASSERT_TRUE(suite->game_->isGameComplete());

    suite->game_->resetGame();
    ASSERT_FALSE(suite->game_->isGameComplete());
    ASSERT_EQ(suite->game_->getOutcome().result, MiniGameResult::IN_PROGRESS);
}

// Test: In standalone mode, EchoWin loops back to EchoIntro
void echoStandaloneRestartAfterWin(SignalEchoTestSuite* suite) {
    suite->game_->getConfig().managedMode = false;
    suite->game_->skipToState(suite->device_.pdn, 4);

    // Advance past 3s win display timer
    suite->tickWithTime(40, 100);

    State* state = suite->game_->getCurrentState();
    ASSERT_EQ(state->getStateId(), ECHO_INTRO);
}

// ============================================
// DIFFICULTY TEST SUITE
// ============================================

class SignalEchoDifficultyTestSuite : public testing::Test {
public:
    void SetUp() override {
        // Shared clock for deterministic timing
        globalClock_ = new NativeClockDriver("test_diff_clock");
        SimpleTimer::setPlatformClock(globalClock_);
    }

    void TearDown() override {
        delete globalClock_;
    }

    DeviceInstance createDeviceWithConfig(SignalEchoConfig config) {
        DeviceInstance instance;
        instance.deviceIndex = 0;
        instance.isHunter = false;

        char idBuffer[5];
        snprintf(idBuffer, sizeof(idBuffer), "%04d", 10);
        instance.deviceId = idBuffer;

        std::string suffix = "_diff_0";

        instance.loggerDriver = new NativeLoggerDriver(LOGGER_DRIVER_NAME + suffix);
        instance.loggerDriver->setSuppressOutput(true);
        instance.clockDriver = new NativeClockDriver(PLATFORM_CLOCK_DRIVER_NAME + suffix);
        instance.displayDriver = new NativeDisplayDriver(DISPLAY_DRIVER_NAME + suffix);
        instance.primaryButtonDriver = new NativeButtonDriver(PRIMARY_BUTTON_DRIVER_NAME + suffix, 0);
        instance.secondaryButtonDriver = new NativeButtonDriver(SECONDARY_BUTTON_DRIVER_NAME + suffix, 1);
        instance.lightDriver = new NativeLightStripDriver(LIGHT_DRIVER_NAME + suffix);
        instance.hapticsDriver = new NativeHapticsDriver(HAPTICS_DRIVER_NAME + suffix, 0);
        instance.serialOutDriver = new NativeSerialDriver(SERIAL_OUT_DRIVER_NAME + suffix);
        instance.serialInDriver = new NativeSerialDriver(SERIAL_IN_DRIVER_NAME + suffix);
        instance.httpClientDriver = new NativeHttpClientDriver(HTTP_CLIENT_DRIVER_NAME + suffix);
        instance.httpClientDriver->setMockServerEnabled(true);
        instance.httpClientDriver->setConnected(true);
        instance.peerCommsDriver = new NativePeerCommsDriver(PEER_COMMS_DRIVER_NAME + suffix);
        instance.storageDriver = new NativePrefsDriver(STORAGE_DRIVER_NAME + suffix);

        DriverConfig pdnConfig = {
            {DISPLAY_DRIVER_NAME, instance.displayDriver},
            {PRIMARY_BUTTON_DRIVER_NAME, instance.primaryButtonDriver},
            {SECONDARY_BUTTON_DRIVER_NAME, instance.secondaryButtonDriver},
            {LIGHT_DRIVER_NAME, instance.lightDriver},
            {HAPTICS_DRIVER_NAME, instance.hapticsDriver},
            {SERIAL_OUT_DRIVER_NAME, instance.serialOutDriver},
            {SERIAL_IN_DRIVER_NAME, instance.serialInDriver},
            {HTTP_CLIENT_DRIVER_NAME, instance.httpClientDriver},
            {PEER_COMMS_DRIVER_NAME, instance.peerCommsDriver},
            {PLATFORM_CLOCK_DRIVER_NAME, instance.clockDriver},
            {LOGGER_DRIVER_NAME, instance.loggerDriver},
            {STORAGE_DRIVER_NAME, instance.storageDriver},
        };

        instance.pdn = PDN::createPDN(pdnConfig);
        instance.pdn->begin();

        instance.player = nullptr;
        instance.quickdrawWirelessManager = nullptr;

        instance.game = new SignalEcho(config);

        AppConfig apps = {
            {StateId(SIGNAL_ECHO_APP_ID), instance.game}
        };
        instance.pdn->loadAppConfig(apps, StateId(SIGNAL_ECHO_APP_ID));

        return instance;
    }

    void destroyDevice(DeviceInstance& device) {
        delete device.game;
        delete device.pdn;
    }

    NativeClockDriver* globalClock_ = nullptr;
};

// Test: Easy config generates 7-element sequences (updated for Wave 18 Cypher Recall)
void echoDiffEasySequenceLength(SignalEchoDifficultyTestSuite* suite) {
    SignalEchoConfig config = SIGNAL_ECHO_EASY;
    config.rngSeed = 42;
    auto device = suite->createDeviceWithConfig(config);
    auto* game = dynamic_cast<SignalEcho*>(device.game);

    ASSERT_EQ(static_cast<int>(game->getSession().currentSequence.size()),
              config.sequenceLength);
    ASSERT_EQ(config.sequenceLength, 7);

    suite->destroyDevice(device);
}

// Test: Easy mode config has 7-length sequences and 3 rounds
void echoDiffEasyConfigParams(SignalEchoDifficultyTestSuite* suite) {
    SignalEchoConfig config = SIGNAL_ECHO_EASY;
    config.rngSeed = 42;
    auto device = suite->createDeviceWithConfig(config);
    auto* game = dynamic_cast<SignalEcho*>(device.game);

    ASSERT_EQ(config.sequenceLength, 7);
    ASSERT_EQ(config.numSequences, 3);
    ASSERT_EQ(config.allowedMistakes, 0);
    ASSERT_EQ(static_cast<int>(game->getSession().currentSequence.size()), 7);

    suite->destroyDevice(device);
}

// Test: Hard config generates 11-element sequences with 5 rounds
void echoDiffHardSequenceLength(SignalEchoDifficultyTestSuite* suite) {
    SignalEchoConfig config = SIGNAL_ECHO_HARD;
    config.rngSeed = 42;
    auto device = suite->createDeviceWithConfig(config);
    auto* game = dynamic_cast<SignalEcho*>(device.game);

    ASSERT_EQ(static_cast<int>(game->getSession().currentSequence.size()),
              config.sequenceLength);
    ASSERT_EQ(config.sequenceLength, 11);
    ASSERT_EQ(config.numSequences, 5);

    suite->destroyDevice(device);
}

// Test: Wrong sequence causes immediate lose (deferred evaluation)
void echoDiffWrongSequenceLoses(SignalEchoDifficultyTestSuite* suite) {
    SignalEchoConfig config = SIGNAL_ECHO_HARD;
    config.rngSeed = 42;
    config.displaySpeedMs = 10;
    auto device = suite->createDeviceWithConfig(config);
    auto* game = dynamic_cast<SignalEcho*>(device.game);

    game->getSession().currentSequence = {true, true, false, false};
    game->getSession().inputIndex = 0;
    game->getSession().playerInput.clear();

    game->skipToState(device.pdn, 2);
    device.pdn->loop();

    // Enter wrong sequence: UP, UP, UP, UP (correct is UP, UP, DOWN, DOWN)
    for (int i = 0; i < 4; i++) {
        device.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
        device.pdn->loop();
    }

    // Wait for evaluation
    for (int i = 0; i < 5; i++) {
        device.pdn->loop();
    }

    State* state = game->getCurrentState();
    ASSERT_EQ(state->getStateId(), ECHO_LOSE);

    suite->destroyDevice(device);
}

// Test: Easy mode win sets outcome correctly
void echoDiffEasyWinOutcome(SignalEchoDifficultyTestSuite* suite) {
    SignalEchoConfig config = SIGNAL_ECHO_EASY;
    config.rngSeed = 42;
    config.displaySpeedMs = 10;
    config.numSequences = 1;
    config.sequenceLength = 2;
    auto device = suite->createDeviceWithConfig(config);
    auto* game = dynamic_cast<SignalEcho*>(device.game);

    game->getSession().currentSequence = {true, false};
    game->getSession().inputIndex = 0;
    game->getSession().currentRound = 0;

    game->skipToState(device.pdn, 2);
    device.pdn->loop();

    // Correct inputs
    device.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    device.pdn->loop();
    device.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    for (int i = 0; i < 5; i++) {
        device.pdn->loop();
    }

    ASSERT_EQ(game->getOutcome().result, MiniGameResult::WON);
    ASSERT_FALSE(game->getOutcome().hardMode);

    suite->destroyDevice(device);
}

// Test: Hard mode win sets hardMode flag
void echoDiffHardWinOutcome(SignalEchoDifficultyTestSuite* suite) {
    SignalEchoConfig config = SIGNAL_ECHO_HARD;
    config.rngSeed = 42;
    config.displaySpeedMs = 10;
    config.numSequences = 1;
    auto device = suite->createDeviceWithConfig(config);
    auto* game = dynamic_cast<SignalEcho*>(device.game);

    game->getSession().currentSequence = {true, false, true, false, true, false, true, false};
    game->getSession().inputIndex = 0;
    game->getSession().currentRound = 0;

    game->skipToState(device.pdn, 2);
    device.pdn->loop();

    // Enter all 8 correct inputs
    for (int i = 0; i < 8; i++) {
        bool expected = game->getSession().currentSequence[i];
        if (expected) {
            device.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
        } else {
            device.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
        }
        device.pdn->loop();
    }

    for (int i = 0; i < 5; i++) {
        device.pdn->loop();
    }

    ASSERT_EQ(game->getOutcome().result, MiniGameResult::WON);
    ASSERT_TRUE(game->getOutcome().hardMode);

    suite->destroyDevice(device);
}

// Test: Life indicator calculation
void echoDiffLifeIndicator(SignalEchoDifficultyTestSuite* suite) {
    ASSERT_EQ(SIGNAL_ECHO_EASY.allowedMistakes, 3);
    ASSERT_EQ(SIGNAL_ECHO_HARD.allowedMistakes, 1);
}

// Test: Wrong input advances to next position (locked in)
void echoDiffWrongInputAdvances(SignalEchoDifficultyTestSuite* suite) {
    SignalEchoConfig config = SIGNAL_ECHO_EASY;
    config.rngSeed = 42;
    config.displaySpeedMs = 10;
    auto device = suite->createDeviceWithConfig(config);
    auto* game = dynamic_cast<SignalEcho*>(device.game);

    game->getSession().currentSequence = {true, false, true, false};
    game->getSession().inputIndex = 0;

    game->skipToState(device.pdn, 2);
    device.pdn->loop();

    // Wrong press at position 0
    device.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    device.pdn->loop();

    ASSERT_EQ(game->getSession().inputIndex, 1);

    suite->destroyDevice(device);
}

// ============================================
// EDGE CASE TESTS (NEW)
// ============================================

// Test: One wrong arrow in sequence causes lose (deferred evaluation)
void echoOneWrongArrowLoses(SignalEchoTestSuite* suite) {
    suite->game_->getConfig().displaySpeedMs = 10;
    suite->game_->getConfig().allowedMistakes = 0;
    suite->game_->getConfig().sequenceLength = 5;
    suite->game_->getSession().currentSequence = {true, true, true, true, true};
    suite->game_->getSession().inputIndex = 0;
    suite->game_->getSession().playerInput.clear();

    suite->game_->skipToState(suite->device_.pdn, 2);
    suite->tick(1);

    // Enter sequence with one mistake: UP, UP, DOWN (wrong), UP, UP
    suite->device_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tick(1);
    suite->device_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tick(1);
    suite->device_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);  // WRONG
    suite->tick(1);
    suite->device_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tick(1);
    suite->device_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tick(5);

    State* state = suite->game_->getCurrentState();
    ASSERT_EQ(state->getStateId(), ECHO_LOSE);
}

// Test: Button press during Intro state is ignored
void echoButtonPressDuringIntroIgnored(SignalEchoTestSuite* suite) {
    ASSERT_EQ(suite->game_->getCurrentStateId(), ECHO_INTRO);

    auto& session = suite->game_->getSession();
    session.inputIndex = 0;

    // Press buttons during intro
    suite->device_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tick(1);
    suite->device_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tick(1);

    // Session should be unchanged
    ASSERT_EQ(session.inputIndex, 0);
    ASSERT_EQ(static_cast<int>(session.playerInput.size()), 0);
    ASSERT_EQ(suite->game_->getCurrentStateId(), ECHO_INTRO);
}

// Test: Button press during ShowSequence state is ignored
void echoButtonPressDuringShowIgnored(SignalEchoTestSuite* suite) {
    suite->game_->skipToState(suite->device_.pdn, 1);
    ASSERT_EQ(suite->game_->getCurrentStateId(), ECHO_SHOW_SEQUENCE);

    auto& session = suite->game_->getSession();
    int initialIndex = session.inputIndex;
    int initialInputs = static_cast<int>(session.playerInput.size());

    // Press buttons during show
    suite->device_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tick(1);
    suite->device_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tick(1);

    // Session should be unchanged
    ASSERT_EQ(session.inputIndex, initialIndex);
    ASSERT_EQ(static_cast<int>(session.playerInput.size()), initialInputs);
}

// Test: Cumulative mode with maximum sequence length (appends 1 element per round)
void echoCumulativeModeMaxLength(SignalEchoTestSuite* suite) {
    SignalEchoConfig config;
    config.sequenceLength = 3;
    config.numSequences = 10;  // Build up to length 12
    config.cumulative = true;
    config.rngSeed = 42;
    config.displaySpeedMs = 10;
    config.allowedMistakes = 0;

    SignalEcho* customGame = new SignalEcho(config);
    customGame->initialize(suite->device_.pdn);

    // First round: length 3
    ASSERT_EQ(static_cast<int>(customGame->getSession().currentSequence.size()), 3);

    // Simulate completing multiple rounds
    for (int round = 0; round < 5; round++) {
        auto& session = customGame->getSession();
        session.playerInput = session.currentSequence;  // correct input
        session.inputIndex = static_cast<int>(session.currentSequence.size());
        customGame->skipToState(suite->device_.pdn, 3);  // Evaluate
        suite->device_.pdn->loop();
    }

    // After 5 rounds, sequence should have grown by 5 (one element appended per round)
    ASSERT_EQ(static_cast<int>(customGame->getSession().currentSequence.size()), 8);

    delete customGame;
}

// Test: Empty sequence edge case (0-length config)
void echoZeroLengthSequenceConfig(SignalEchoTestSuite* suite) {
    SignalEchoConfig config;
    config.sequenceLength = 0;  // Invalid config
    config.numSequences = 1;
    config.rngSeed = 42;

    SignalEcho* customGame = new SignalEcho(config);
    customGame->initialize(suite->device_.pdn);

    // Should generate empty sequence
    ASSERT_EQ(static_cast<int>(customGame->getSession().currentSequence.size()), 0);

    // Skip to input state
    customGame->skipToState(suite->device_.pdn, 2);
    suite->tick(1);

    // Input index should immediately equal length, triggering evaluate
    ASSERT_EQ(customGame->getSession().inputIndex, 0);

    delete customGame;
}

#endif // NATIVE_BUILD
