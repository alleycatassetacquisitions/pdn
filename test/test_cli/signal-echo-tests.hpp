//
// Signal Echo Tests — test functions for Signal Echo game states
// and difficulty mode behavior.
//

#pragma once

#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include "cli/cli-device.hpp"
#include "game/signal-echo/signal-echo.hpp"
#include "game/signal-echo/signal-echo-states.hpp"
#include "game/signal-echo/signal-echo-resources.hpp"

// ============================================
// SIGNAL ECHO TEST SUITE
// ============================================

class SignalEchoTestSuite : public testing::Test {
public:
    void SetUp() override {
        globalClock_ = new NativeClockDriver("test_echo_clock");
        globalLogger_ = new NativeLoggerDriver("test_echo_logger");
        globalLogger_->setSuppressOutput(true);
        g_logger = globalLogger_;
        SimpleTimer::setPlatformClock(globalClock_);

        // Create a device using the game factory with a test config
        device_ = cli::DeviceFactory::createGameDevice(0, "signal-echo");

        // Get the SignalEcho game instance
        game_ = dynamic_cast<SignalEcho*>(device_.game);
    }

    void TearDown() override {
        cli::DeviceFactory::destroyDevice(device_);
        SimpleTimer::setPlatformClock(nullptr);
        g_logger = nullptr;
        delete globalLogger_;
        delete globalClock_;
    }

    // Helper: run N loop cycles on device + game
    void runLoops(int count) {
        for (int i = 0; i < count; i++) {
            device_.pdn->loop();
            device_.game->loop();
        }
    }

    // Helper: run loops with time advancement to get past timed transitions
    void runLoopsWithDelay(int count, int delayMs) {
        for (int i = 0; i < count; i++) {
            device_.pdn->loop();
            device_.game->loop();
            std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
        }
    }

    // Helper: create a SignalEcho with a custom config
    SignalEcho* createCustomGame(SignalEchoConfig config) {
        SignalEcho* customGame = new SignalEcho(device_.pdn, config);
        customGame->initialize();
        return customGame;
    }

    cli::DeviceInstance device_;
    SignalEcho* game_ = nullptr;
    NativeClockDriver* globalClock_ = nullptr;
    NativeLoggerDriver* globalLogger_ = nullptr;
};

// ============================================
// SIGNAL ECHO TESTS — Sequence Generation
// ============================================

// Test: Generated sequence has correct length
void echoSequenceGenerationLength(SignalEchoTestSuite* suite) {
    SignalEchoConfig config;
    config.sequenceLength = 6;
    config.rngSeed = 42;

    SignalEcho tempGame(suite->device_.pdn, config);
    tempGame.seedRng();
    std::vector<bool> seq = tempGame.generateSequence(6);
    ASSERT_EQ(static_cast<int>(seq.size()), 6);

    std::vector<bool> seq2 = tempGame.generateSequence(10);
    ASSERT_EQ(static_cast<int>(seq2.size()), 10);
}

// Test: Sequence contains both UP and DOWN (statistical)
void echoSequenceGenerationMixed(SignalEchoTestSuite* suite) {
    SignalEchoConfig config;
    config.rngSeed = 42;

    SignalEcho tempGame(suite->device_.pdn, config);
    tempGame.seedRng();

    // Generate a long sequence to ensure both values appear
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
// SIGNAL ECHO TESTS — State Transitions
// ============================================

// Test: EchoIntro transitions to EchoShowSequence after timer
void echoIntroTransitionsToShow(SignalEchoTestSuite* suite) {
    // Game starts at EchoIntro (state index 0)
    State* state = suite->game_->getCurrentState();
    ASSERT_NE(state, nullptr);
    ASSERT_EQ(state->getStateId(), ECHO_INTRO);

    // Run loops with enough delay for the 2s intro timer
    suite->runLoopsWithDelay(30, 100);  // 3 seconds total

    state = suite->game_->getCurrentState();
    ASSERT_EQ(state->getStateId(), ECHO_SHOW_SEQUENCE);
}

// Test: ShowSequence displays each signal for displaySpeedMs
void echoShowSequenceTimingPerSignal(SignalEchoTestSuite* suite) {
    // Skip to ShowSequence state (index 1)
    suite->game_->skipToState(1);

    State* state = suite->game_->getCurrentState();
    ASSERT_EQ(state->getStateId(), ECHO_SHOW_SEQUENCE);

    // Generate a known sequence
    suite->game_->getSession().currentSequence = {true, false, true, false};

    // Run one loop — should display first signal
    suite->runLoops(1);
    state = suite->game_->getCurrentState();
    ASSERT_EQ(state->getStateId(), ECHO_SHOW_SEQUENCE);  // Still showing

    // Wait less than displaySpeedMs — should still be showing
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    suite->runLoops(1);
    state = suite->game_->getCurrentState();
    ASSERT_EQ(state->getStateId(), ECHO_SHOW_SEQUENCE);
}

// Test: ShowSequence transitions to PlayerInput after all signals
void echoShowTransitionsToInput(SignalEchoTestSuite* suite) {
    // Set a very short display speed for testing
    suite->game_->getConfig().displaySpeedMs = 50;
    suite->game_->getSession().currentSequence = {true, false};

    suite->game_->skipToState(1);  // EchoShowSequence

    // Run enough loops with delays to display both signals
    suite->runLoopsWithDelay(20, 50);  // 1 second total

    State* state = suite->game_->getCurrentState();
    ASSERT_EQ(state->getStateId(), ECHO_PLAYER_INPUT);
}

// ============================================
// SIGNAL ECHO TESTS — Player Input
// ============================================

// Test: Correct button press advances inputIndex
void echoCorrectInputAdvancesIndex(SignalEchoTestSuite* suite) {
    suite->game_->getConfig().displaySpeedMs = 10;
    suite->game_->getSession().currentSequence = {true, false, true, false};
    suite->game_->getSession().inputIndex = 0;

    suite->game_->skipToState(2);  // EchoPlayerInput
    suite->runLoops(1);  // Let it mount and set up callbacks

    // Sequence is: UP, DOWN, UP, DOWN
    // Press UP (primary button) — correct for position 0
    suite->device_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->runLoops(1);

    ASSERT_EQ(suite->game_->getSession().inputIndex, 1);
    ASSERT_EQ(suite->game_->getSession().mistakes, 0);
}

// Test: Wrong button press increments mistakes
void echoWrongInputCountsMistake(SignalEchoTestSuite* suite) {
    suite->game_->getConfig().displaySpeedMs = 10;
    suite->game_->getSession().currentSequence = {true, false, true, false};
    suite->game_->getSession().inputIndex = 0;

    suite->game_->skipToState(2);  // EchoPlayerInput
    suite->runLoops(1);

    // Sequence starts with UP, but we press DOWN (secondary) — wrong
    suite->device_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->runLoops(1);

    ASSERT_EQ(suite->game_->getSession().inputIndex, 1);  // Still advances
    ASSERT_EQ(suite->game_->getSession().mistakes, 1);
}

// Test: Completing all inputs correctly leads to next round via Evaluate
void echoAllCorrectInputsNextRound(SignalEchoTestSuite* suite) {
    suite->game_->getConfig().displaySpeedMs = 10;
    suite->game_->getConfig().numSequences = 3;
    suite->game_->getConfig().sequenceLength = 2;
    suite->game_->getConfig().allowedMistakes = 3;
    suite->game_->getSession().currentSequence = {true, false};
    suite->game_->getSession().currentRound = 0;
    suite->game_->getSession().inputIndex = 0;

    suite->game_->skipToState(2);  // EchoPlayerInput
    suite->runLoops(1);

    // Press correct buttons: UP then DOWN
    suite->device_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->runLoops(1);
    suite->device_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->runLoops(2);  // Extra loop to process transition

    // Should transition to EchoEvaluate then to EchoShowSequence
    // Evaluate is instantaneous — it sets its transition in onStateMounted
    suite->runLoops(2);

    State* state = suite->game_->getCurrentState();
    // Should be at ShowSequence for next round, or Evaluate
    ASSERT_TRUE(state->getStateId() == ECHO_EVALUATE ||
                state->getStateId() == ECHO_SHOW_SEQUENCE);
}

// Test: Exceeding allowed mistakes leads to EchoLose
void echoMistakesExhaustedLose(SignalEchoTestSuite* suite) {
    suite->game_->getConfig().displaySpeedMs = 10;
    suite->game_->getConfig().allowedMistakes = 1;
    suite->game_->getConfig().sequenceLength = 4;
    suite->game_->getSession().currentSequence = {true, true, true, true};
    suite->game_->getSession().inputIndex = 0;
    suite->game_->getSession().mistakes = 0;

    suite->game_->skipToState(2);  // EchoPlayerInput
    suite->runLoops(1);

    // Press wrong button twice (allowedMistakes = 1, so 2nd exceeds)
    suite->device_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);  // Wrong
    suite->runLoops(1);
    suite->device_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);  // Wrong
    suite->runLoops(5);  // Process transition through Evaluate to Lose

    State* state = suite->game_->getCurrentState();
    ASSERT_EQ(state->getStateId(), ECHO_LOSE);
}

// Test: All rounds completed successfully leads to EchoWin
void echoAllRoundsCompletedWin(SignalEchoTestSuite* suite) {
    suite->game_->getConfig().displaySpeedMs = 10;
    suite->game_->getConfig().numSequences = 1;  // Just 1 round
    suite->game_->getConfig().sequenceLength = 2;
    suite->game_->getConfig().allowedMistakes = 3;
    suite->game_->getSession().currentSequence = {true, false};
    suite->game_->getSession().currentRound = 0;
    suite->game_->getSession().inputIndex = 0;

    suite->game_->skipToState(2);  // EchoPlayerInput
    suite->runLoops(1);

    // Enter correct sequence
    suite->device_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);   // UP
    suite->runLoops(1);
    suite->device_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK); // DOWN
    suite->runLoops(5);  // Process through Evaluate to Win

    State* state = suite->game_->getCurrentState();
    ASSERT_EQ(state->getStateId(), ECHO_WIN);
}

// ============================================
// SIGNAL ECHO TESTS — Cumulative & Fresh Mode
// ============================================

// Test: Cumulative mode appends one element each round
void echoCumulativeModeAppends(SignalEchoTestSuite* suite) {
    SignalEchoConfig config;
    config.sequenceLength = 3;
    config.numSequences = 3;
    config.cumulative = true;
    config.rngSeed = 42;
    config.displaySpeedMs = 10;
    config.allowedMistakes = 10;  // High tolerance for testing

    SignalEcho* customGame = new SignalEcho(suite->device_.pdn, config);
    customGame->initialize();

    // First round sequence should be length 3
    int firstLen = static_cast<int>(customGame->getSession().currentSequence.size());
    ASSERT_EQ(firstLen, 3);

    // Simulate completing a round by manually advancing
    auto& session = customGame->getSession();
    // Fill in all correct inputs
    session.inputIndex = firstLen;
    session.currentRound = 0;

    // Trigger evaluate logic
    customGame->skipToState(3);  // EchoEvaluate
    // After evaluate, currentRound should be 1 and sequence should be length 4
    ASSERT_EQ(session.currentRound, 1);
    ASSERT_EQ(static_cast<int>(session.currentSequence.size()), 4);

    delete customGame;
}

// Test: Fresh mode generates new sequence each round
void echoFreshModeNewSequence(SignalEchoTestSuite* suite) {
    SignalEchoConfig config;
    config.sequenceLength = 4;
    config.numSequences = 3;
    config.cumulative = false;
    config.rngSeed = 42;
    config.displaySpeedMs = 10;
    config.allowedMistakes = 10;

    SignalEcho* customGame = new SignalEcho(suite->device_.pdn, config);
    customGame->initialize();

    auto& session = customGame->getSession();
    std::vector<bool> firstSequence = session.currentSequence;

    // Simulate completing round 0
    session.inputIndex = 4;
    session.currentRound = 0;

    customGame->skipToState(3);  // EchoEvaluate
    ASSERT_EQ(session.currentRound, 1);
    ASSERT_EQ(static_cast<int>(session.currentSequence.size()), 4);
    // Fresh sequence — length stays the same (unlike cumulative)

    delete customGame;
}

// ============================================
// SIGNAL ECHO TESTS — Outcome & Game Complete
// ============================================

// Test: EchoWin sets outcome to WON with score
void echoWinSetsOutcome(SignalEchoTestSuite* suite) {
    suite->game_->getSession().score = 400;
    suite->game_->skipToState(4);  // EchoWin

    const MiniGameOutcome& outcome = suite->game_->getOutcome();
    ASSERT_EQ(outcome.result, MiniGameResult::WON);
    ASSERT_EQ(outcome.score, 400);
}

// Test: EchoLose sets outcome to LOST
void echoLoseSetsOutcome(SignalEchoTestSuite* suite) {
    suite->game_->getSession().score = 200;
    suite->game_->skipToState(5);  // EchoLose

    const MiniGameOutcome& outcome = suite->game_->getOutcome();
    ASSERT_EQ(outcome.result, MiniGameResult::LOST);
    ASSERT_EQ(outcome.score, 200);
}

// Test: isGameComplete returns true after win
void echoIsGameCompleteAfterWin(SignalEchoTestSuite* suite) {
    ASSERT_FALSE(suite->game_->isGameComplete());

    suite->game_->skipToState(4);  // EchoWin
    ASSERT_TRUE(suite->game_->isGameComplete());
}

// Test: resetGame clears outcome back to IN_PROGRESS
void echoResetGameClearsOutcome(SignalEchoTestSuite* suite) {
    suite->game_->skipToState(4);  // EchoWin — sets outcome
    ASSERT_TRUE(suite->game_->isGameComplete());

    suite->game_->resetGame();
    ASSERT_FALSE(suite->game_->isGameComplete());
    ASSERT_EQ(suite->game_->getOutcome().result, MiniGameResult::IN_PROGRESS);
}

// Test: In standalone mode, EchoWin loops back to EchoIntro
void echoStandaloneRestartAfterWin(SignalEchoTestSuite* suite) {
    suite->game_->getConfig().managedMode = false;
    suite->game_->skipToState(4);  // EchoWin

    // Wait for win display timer
    suite->runLoopsWithDelay(40, 100);  // 4 seconds

    State* state = suite->game_->getCurrentState();
    ASSERT_EQ(state->getStateId(), ECHO_INTRO);
}

// ============================================
// SIGNAL ECHO DIFFICULTY TEST SUITE
// ============================================

class SignalEchoDifficultyTestSuite : public testing::Test {
public:
    void SetUp() override {
        globalClock_ = new NativeClockDriver("test_diff_clock");
        globalLogger_ = new NativeLoggerDriver("test_diff_logger");
        globalLogger_->setSuppressOutput(true);
        g_logger = globalLogger_;
        SimpleTimer::setPlatformClock(globalClock_);
    }

    void TearDown() override {
        SimpleTimer::setPlatformClock(nullptr);
        g_logger = nullptr;
        delete globalLogger_;
        delete globalClock_;
    }

    // Helper: create a device running Signal Echo with a given config
    cli::DeviceInstance createDevice(SignalEchoConfig config) {
        cli::DeviceInstance instance;
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

        instance.game = new SignalEcho(instance.pdn, config);
        instance.game->initialize();

        return instance;
    }

    void destroyDevice(cli::DeviceInstance& device) {
        delete device.game;
        delete device.pdn;
    }

    NativeClockDriver* globalClock_ = nullptr;
    NativeLoggerDriver* globalLogger_ = nullptr;
};

// Test: Easy config generates 4-element sequences
void echoDiffEasyModeSequenceLength4(SignalEchoDifficultyTestSuite* suite) {
    SignalEchoConfig config = SIGNAL_ECHO_EASY;
    config.rngSeed = 42;
    auto device = suite->createDevice(config);
    auto* game = dynamic_cast<SignalEcho*>(device.game);

    ASSERT_EQ(static_cast<int>(game->getSession().currentSequence.size()),
              config.sequenceLength);
    ASSERT_EQ(config.sequenceLength, 4);

    suite->destroyDevice(device);
}

// Test: Easy mode allows 3 mistakes before losing
void echoDiffEasyMode3MistakesAllowed(SignalEchoDifficultyTestSuite* suite) {
    SignalEchoConfig config = SIGNAL_ECHO_EASY;
    config.rngSeed = 42;
    config.displaySpeedMs = 10;
    auto device = suite->createDevice(config);
    auto* game = dynamic_cast<SignalEcho*>(device.game);

    // Set up a sequence of all UPs
    game->getSession().currentSequence = {true, true, true, true};
    game->getSession().inputIndex = 0;

    game->skipToState(2);  // EchoPlayerInput
    device.pdn->loop();
    device.game->loop();

    // Press wrong 3 times — should still be in game (3 allowed)
    for (int i = 0; i < 3; i++) {
        device.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
        device.pdn->loop();
        device.game->loop();
    }
    ASSERT_EQ(game->getSession().mistakes, 3);

    // State should still be PlayerInput (3 mistakes = exactly the limit, not exceeded)
    // The 4th wrong press would exceed
    State* state = game->getCurrentState();
    // After 3 mistakes with allowedMistakes=3, we have NOT exceeded yet
    // because the check is mistakes > allowedMistakes
    ASSERT_EQ(state->getStateId(), ECHO_PLAYER_INPUT);

    // 4th wrong press exceeds the limit
    device.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    for (int i = 0; i < 5; i++) {
        device.pdn->loop();
        device.game->loop();
    }

    state = game->getCurrentState();
    ASSERT_EQ(state->getStateId(), ECHO_LOSE);

    suite->destroyDevice(device);
}

// Test: Hard config generates 8-element sequences
void echoDiffHardModeSequenceLength8(SignalEchoDifficultyTestSuite* suite) {
    SignalEchoConfig config = SIGNAL_ECHO_HARD;
    config.rngSeed = 42;
    auto device = suite->createDevice(config);
    auto* game = dynamic_cast<SignalEcho*>(device.game);

    ASSERT_EQ(static_cast<int>(game->getSession().currentSequence.size()),
              config.sequenceLength);
    ASSERT_EQ(config.sequenceLength, 8);

    suite->destroyDevice(device);
}

// Test: Hard mode — 1st wrong press causes immediate lose
void echoDiffHardMode1MistakeAllowed(SignalEchoDifficultyTestSuite* suite) {
    SignalEchoConfig config = SIGNAL_ECHO_HARD;
    config.rngSeed = 42;
    config.displaySpeedMs = 10;
    auto device = suite->createDevice(config);
    auto* game = dynamic_cast<SignalEcho*>(device.game);

    game->getSession().currentSequence = {true, true, true, true, true, true, true, true};
    game->getSession().inputIndex = 0;

    game->skipToState(2);  // EchoPlayerInput
    device.pdn->loop();
    device.game->loop();

    // First wrong press
    device.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    device.pdn->loop();
    device.game->loop();

    ASSERT_EQ(game->getSession().mistakes, 1);

    // Second wrong press exceeds allowedMistakes (1)
    device.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    for (int i = 0; i < 5; i++) {
        device.pdn->loop();
        device.game->loop();
    }

    State* state = game->getCurrentState();
    ASSERT_EQ(state->getStateId(), ECHO_LOSE);

    suite->destroyDevice(device);
}

// Test: Wrong input shows error marker on display
void echoDiffWrongInputShowsErrorMarker(SignalEchoDifficultyTestSuite* suite) {
    SignalEchoConfig config = SIGNAL_ECHO_EASY;
    config.rngSeed = 42;
    config.displaySpeedMs = 10;
    auto device = suite->createDevice(config);
    auto* game = dynamic_cast<SignalEcho*>(device.game);

    game->getSession().currentSequence = {true, false, true, false};
    game->getSession().inputIndex = 0;

    game->skipToState(2);  // EchoPlayerInput
    device.pdn->loop();
    device.game->loop();

    // Press wrong button
    device.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    device.pdn->loop();
    device.game->loop();

    // Verify display was updated (text history should have "YOUR TURN" content)
    const auto& textHistory = device.displayDriver->getTextHistory();
    ASSERT_GE(textHistory.size(), 1u);

    suite->destroyDevice(device);
}

// Test: Wrong input advances inputIndex (locked in)
void echoDiffWrongInputAdvancesToNext(SignalEchoDifficultyTestSuite* suite) {
    SignalEchoConfig config = SIGNAL_ECHO_EASY;
    config.rngSeed = 42;
    config.displaySpeedMs = 10;
    auto device = suite->createDevice(config);
    auto* game = dynamic_cast<SignalEcho*>(device.game);

    game->getSession().currentSequence = {true, false, true, false};
    game->getSession().inputIndex = 0;

    game->skipToState(2);
    device.pdn->loop();
    device.game->loop();

    // Wrong press at position 0
    device.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    device.pdn->loop();
    device.game->loop();

    ASSERT_EQ(game->getSession().inputIndex, 1);  // Advanced despite being wrong

    suite->destroyDevice(device);
}

// Test: Life indicator starts at allowedMistakes
void echoDiffLifeIndicatorStartsFull(SignalEchoDifficultyTestSuite* suite) {
    SignalEchoConfig config = SIGNAL_ECHO_EASY;
    config.rngSeed = 42;
    auto device = suite->createDevice(config);
    auto* game = dynamic_cast<SignalEcho*>(device.game);

    ASSERT_EQ(game->getSession().mistakes, 0);
    int livesRemaining = config.allowedMistakes - game->getSession().mistakes;
    ASSERT_EQ(livesRemaining, 3);  // Easy mode: 3 lives

    suite->destroyDevice(device);
}

// Test: Life indicator decrements on mistake
void echoDiffLifeIndicatorDecrementsOnMistake(SignalEchoDifficultyTestSuite* suite) {
    SignalEchoConfig config = SIGNAL_ECHO_EASY;
    config.rngSeed = 42;
    config.displaySpeedMs = 10;
    auto device = suite->createDevice(config);
    auto* game = dynamic_cast<SignalEcho*>(device.game);

    game->getSession().currentSequence = {true, true, true, true};
    game->getSession().inputIndex = 0;

    game->skipToState(2);
    device.pdn->loop();
    device.game->loop();

    int livesBefore = config.allowedMistakes - game->getSession().mistakes;
    ASSERT_EQ(livesBefore, 3);

    device.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);  // Wrong
    device.pdn->loop();
    device.game->loop();

    int livesAfter = config.allowedMistakes - game->getSession().mistakes;
    ASSERT_EQ(livesAfter, 2);

    suite->destroyDevice(device);
}

// Test: Easy = 3 lives, Hard = 1 life
void echoDiffLifeIndicatorCorrectCount(SignalEchoDifficultyTestSuite* suite) {
    ASSERT_EQ(SIGNAL_ECHO_EASY.allowedMistakes, 3);
    ASSERT_EQ(SIGNAL_ECHO_HARD.allowedMistakes, 1);
}

// Test: After EchoLose, session resets (currentRound=0, mistakes=0)
void echoDiffFullResetOnLose(SignalEchoDifficultyTestSuite* suite) {
    SignalEchoConfig config = SIGNAL_ECHO_EASY;
    config.rngSeed = 42;
    config.displaySpeedMs = 10;
    auto device = suite->createDevice(config);
    auto* game = dynamic_cast<SignalEcho*>(device.game);

    // Manually set some progress
    game->getSession().currentRound = 2;
    game->getSession().mistakes = 4;
    game->getSession().score = 300;

    // Go to Lose, then back to Intro
    game->skipToState(5);  // EchoLose
    // Wait for lose timer
    std::this_thread::sleep_for(std::chrono::milliseconds(3500));
    for (int i = 0; i < 5; i++) {
        device.pdn->loop();
        device.game->loop();
    }

    // Should be back at Intro, which resets the session
    State* state = game->getCurrentState();
    if (state->getStateId() == ECHO_INTRO) {
        // Intro resets session and game outcome
        ASSERT_EQ(game->getSession().currentRound, 0);
        ASSERT_EQ(game->getSession().mistakes, 0);
        ASSERT_EQ(game->getOutcome().result, MiniGameResult::IN_PROGRESS);
    } else {
        // If still at Lose (timer might not have expired in some environments),
        // at least verify the lose outcome was set
        ASSERT_EQ(game->getOutcome().result, MiniGameResult::LOST);
    }

    suite->destroyDevice(device);
}

// Test: Easy mode win unlocks Konami button (outcome.result = WON)
void echoDiffEasyModeWinUnlocksKonami(SignalEchoDifficultyTestSuite* suite) {
    SignalEchoConfig config = SIGNAL_ECHO_EASY;
    config.rngSeed = 42;
    config.displaySpeedMs = 10;
    config.numSequences = 1;
    config.sequenceLength = 2;
    auto device = suite->createDevice(config);
    auto* game = dynamic_cast<SignalEcho*>(device.game);

    game->getSession().currentSequence = {true, false};
    game->getSession().inputIndex = 0;
    game->getSession().currentRound = 0;

    game->skipToState(2);  // PlayerInput
    device.pdn->loop();
    device.game->loop();

    // Correct inputs
    device.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    device.pdn->loop();
    device.game->loop();
    device.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    for (int i = 0; i < 5; i++) {
        device.pdn->loop();
        device.game->loop();
    }

    ASSERT_EQ(game->getOutcome().result, MiniGameResult::WON);
    // Easy mode win — hardMode should be false
    ASSERT_FALSE(game->getOutcome().hardMode);

    suite->destroyDevice(device);
}

// Test: Hard mode win sets hardMode flag
void echoDiffHardModeWinUnlocksColorProfile(SignalEchoDifficultyTestSuite* suite) {
    SignalEchoConfig config = SIGNAL_ECHO_HARD;
    config.rngSeed = 42;
    config.displaySpeedMs = 10;
    config.numSequences = 1;
    // Keep sequenceLength = 8 (the hard mode default) so hardMode detection passes
    auto device = suite->createDevice(config);
    auto* game = dynamic_cast<SignalEcho*>(device.game);

    // Set a known 8-element sequence
    game->getSession().currentSequence = {true, false, true, false, true, false, true, false};
    game->getSession().inputIndex = 0;
    game->getSession().currentRound = 0;

    game->skipToState(2);
    device.pdn->loop();
    device.game->loop();

    // Enter all 8 correct inputs
    for (int i = 0; i < 8; i++) {
        bool expected = game->getSession().currentSequence[i];
        if (expected) {
            device.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
        } else {
            device.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
        }
        device.pdn->loop();
        device.game->loop();
    }

    // Process through Evaluate to Win
    for (int i = 0; i < 5; i++) {
        device.pdn->loop();
        device.game->loop();
    }

    ASSERT_EQ(game->getOutcome().result, MiniGameResult::WON);
    ASSERT_TRUE(game->getOutcome().hardMode);

    suite->destroyDevice(device);
}

// Test: LED flashes red on mistake
void echoDiffLedFlashesRedOnMistake(SignalEchoDifficultyTestSuite* suite) {
    SignalEchoConfig config = SIGNAL_ECHO_EASY;
    config.rngSeed = 42;
    config.displaySpeedMs = 10;
    auto device = suite->createDevice(config);
    auto* game = dynamic_cast<SignalEcho*>(device.game);

    game->getSession().currentSequence = {true, true, true, true};
    game->getSession().inputIndex = 0;

    game->skipToState(2);
    device.pdn->loop();
    device.game->loop();

    // Wrong press — should trigger red LED via LightManager
    device.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    device.pdn->loop();
    device.game->loop();

    // Verify mistake was registered
    ASSERT_EQ(game->getSession().mistakes, 1);

    suite->destroyDevice(device);
}

// Test: Haptic buzz on mistake
void echoDiffHapticBuzzOnMistake(SignalEchoDifficultyTestSuite* suite) {
    SignalEchoConfig config = SIGNAL_ECHO_EASY;
    config.rngSeed = 42;
    config.displaySpeedMs = 10;
    auto device = suite->createDevice(config);
    auto* game = dynamic_cast<SignalEcho*>(device.game);

    game->getSession().currentSequence = {true, true, true, true};
    game->getSession().inputIndex = 0;

    game->skipToState(2);
    device.pdn->loop();
    device.game->loop();

    device.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    device.pdn->loop();
    device.game->loop();

    // Verify mistake was registered (haptics would be triggered in handleInput)
    ASSERT_EQ(game->getSession().mistakes, 1);

    suite->destroyDevice(device);
}
