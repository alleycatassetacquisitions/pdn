#pragma once

#include <cstdint>
#include <gtest/gtest.h>
#include "cli/cli-event-logger.hpp"
#include "cli/cli-device.hpp"
#include "cli/cli-serial-broker.hpp"
#include "device/drivers/native/native-peer-broker.hpp"

// ============================================================
// EventLogger Test Suite
// ============================================================

class EventLoggerTestSuite : public testing::Test {
public:
    cli::EventLogger logger;

    void SetUp() override {
        logger.clear();
    }
};

// --- EventLogger unit tests ---

void eventLoggerLogAndRetrieve(EventLoggerTestSuite* suite) {
    cli::Event e;
    e.timestampMs = 100;
    e.type = cli::EventType::STATE_CHANGE;
    e.deviceIndex = 0;
    e.fromState = "Idle";
    e.toState = "HandshakeInitiate";
    suite->logger.log(e);

    ASSERT_EQ(suite->logger.size(), 1u);
    auto all = suite->logger.getAll();
    ASSERT_EQ(all[0].fromState, "Idle");
    ASSERT_EQ(all[0].toState, "HandshakeInitiate");
}

void eventLoggerFilterByType(EventLoggerTestSuite* suite) {
    cli::Event e1;
    e1.type = cli::EventType::SERIAL_TX;
    e1.deviceIndex = 0;
    e1.message = "cdev:7:6";
    suite->logger.log(e1);

    cli::Event e2;
    e2.type = cli::EventType::SERIAL_RX;
    e2.deviceIndex = 1;
    e2.message = "cdev:7:6";
    suite->logger.log(e2);

    cli::Event e3;
    e3.type = cli::EventType::SERIAL_TX;
    e3.deviceIndex = 0;
    e3.message = "hb";
    suite->logger.log(e3);

    auto txEvents = suite->logger.getByType(cli::EventType::SERIAL_TX);
    ASSERT_EQ(txEvents.size(), 2u);

    auto rxEvents = suite->logger.getByType(cli::EventType::SERIAL_RX);
    ASSERT_EQ(rxEvents.size(), 1u);
}

void eventLoggerFilterByDevice(EventLoggerTestSuite* suite) {
    cli::Event e1;
    e1.type = cli::EventType::DISPLAY_TEXT;
    e1.deviceIndex = 0;
    e1.message = "READY";
    suite->logger.log(e1);

    cli::Event e2;
    e2.type = cli::EventType::DISPLAY_TEXT;
    e2.deviceIndex = 1;
    e2.message = "SIGNAL ECHO";
    suite->logger.log(e2);

    auto dev0 = suite->logger.getByDevice(0);
    ASSERT_EQ(dev0.size(), 1u);
    ASSERT_EQ(dev0[0].message, "READY");
}

void eventLoggerFilterByTypeAndDevice(EventLoggerTestSuite* suite) {
    cli::Event e1{0, cli::EventType::SERIAL_TX, 0, "", "", "", "cdev:7:6", ""};
    cli::Event e2{0, cli::EventType::SERIAL_TX, 1, "", "", "", "hb", ""};
    cli::Event e3{0, cli::EventType::SERIAL_RX, 0, "", "", "", "cdev:7:6", ""};
    suite->logger.log(e1);
    suite->logger.log(e2);
    suite->logger.log(e3);

    auto result = suite->logger.getByTypeAndDevice(cli::EventType::SERIAL_TX, 0);
    ASSERT_EQ(result.size(), 1u);
    ASSERT_EQ(result[0].message, "cdev:7:6");
}

void eventLoggerGetLast(EventLoggerTestSuite* suite) {
    cli::Event e1;
    e1.type = cli::EventType::STATE_CHANGE;
    e1.deviceIndex = 0;
    e1.toState = "Idle";
    suite->logger.log(e1);

    cli::Event e2;
    e2.type = cli::EventType::STATE_CHANGE;
    e2.deviceIndex = 0;
    e2.toState = "HandshakeInitiate";
    suite->logger.log(e2);

    auto last = suite->logger.getLast(cli::EventType::STATE_CHANGE);
    ASSERT_EQ(last.toState, "HandshakeInitiate");

    auto lastDev0 = suite->logger.getLast(cli::EventType::STATE_CHANGE, 0);
    ASSERT_EQ(lastDev0.toState, "HandshakeInitiate");
}

void eventLoggerHasEvent(EventLoggerTestSuite* suite) {
    cli::Event e;
    e.type = cli::EventType::SERIAL_TX;
    e.deviceIndex = 0;
    e.message = "cdev:7:6";
    suite->logger.log(e);

    ASSERT_TRUE(suite->logger.hasEvent(cli::EventType::SERIAL_TX, "cdev:"));
    ASSERT_FALSE(suite->logger.hasEvent(cli::EventType::SERIAL_TX, "hb"));
    ASSERT_FALSE(suite->logger.hasEvent(cli::EventType::SERIAL_RX, "cdev:"));
}

void eventLoggerCount(EventLoggerTestSuite* suite) {
    for (int i = 0; i < 3; i++) {
        cli::Event e;
        e.type = cli::EventType::DISPLAY_TEXT;
        e.deviceIndex = 0;
        suite->logger.log(e);
    }
    cli::Event e2;
    e2.type = cli::EventType::DISPLAY_TEXT;
    e2.deviceIndex = 1;
    suite->logger.log(e2);

    ASSERT_EQ(suite->logger.count(cli::EventType::DISPLAY_TEXT), 4u);
    ASSERT_EQ(suite->logger.count(cli::EventType::DISPLAY_TEXT, 0), 3u);
    ASSERT_EQ(suite->logger.count(cli::EventType::DISPLAY_TEXT, 1), 1u);
}

void eventLoggerClear(EventLoggerTestSuite* suite) {
    cli::Event e;
    e.type = cli::EventType::STATE_CHANGE;
    suite->logger.log(e);
    ASSERT_EQ(suite->logger.size(), 1u);

    suite->logger.clear();
    ASSERT_EQ(suite->logger.size(), 0u);
}

void eventLoggerFormatEvent(EventLoggerTestSuite* suite) {
    cli::Event e;
    e.timestampMs = 33;
    e.type = cli::EventType::SERIAL_TX;
    e.deviceIndex = 1;
    e.message = "cdev:7:6";

    std::string formatted = suite->logger.formatEvent(e);
    ASSERT_TRUE(formatted.find("0033ms") != std::string::npos);
    ASSERT_TRUE(formatted.find("SERIAL_TX") != std::string::npos);
    ASSERT_TRUE(formatted.find("dev=1") != std::string::npos);
    ASSERT_TRUE(formatted.find("cdev:7:6") != std::string::npos);
}

void eventLoggerWriteToFile(EventLoggerTestSuite* suite) {
    cli::Event e;
    e.timestampMs = 100;
    e.type = cli::EventType::STATE_CHANGE;
    e.deviceIndex = 0;
    e.fromState = "Idle";
    e.toState = "HandshakeInitiate";
    suite->logger.log(e);

    std::string path = "/tmp/eventlogger_test_output.txt";
    ASSERT_TRUE(suite->logger.writeToFile(path));

    // Verify file was created and has content
    FILE* f = fopen(path.c_str(), "r");
    ASSERT_NE(f, nullptr);
    char buf[256];
    ASSERT_NE(fgets(buf, sizeof(buf), f), nullptr);
    fclose(f);

    std::string line(buf);
    ASSERT_TRUE(line.find("STATE_CHANGE") != std::string::npos);
    ASSERT_TRUE(line.find("Idle") != std::string::npos);

    remove(path.c_str());
}

// ============================================================
// Driver Callback Test Suite
// ============================================================

class DriverCallbackTestSuite : public testing::Test {
public:
    void SetUp() override {
        serialDriver = new NativeSerialDriver("test_serial");
        displayDriver = new NativeDisplayDriver("test_display");
    }

    void TearDown() override {
        delete serialDriver;
        delete displayDriver;
    }

    NativeSerialDriver* serialDriver = nullptr;
    NativeDisplayDriver* displayDriver = nullptr;
};

void serialDriverWriteCallbackFires(DriverCallbackTestSuite* suite) {
    std::string captured;
    suite->serialDriver->setWriteCallback([&captured](const std::string& msg) {
        captured = msg;
    });

    suite->serialDriver->println("hello");
    ASSERT_EQ(captured, "hello");
}

void serialDriverWriteCallbackCharPtr(DriverCallbackTestSuite* suite) {
    std::string captured;
    suite->serialDriver->setWriteCallback([&captured](const std::string& msg) {
        captured = msg;
    });

    char msg[] = "world";
    suite->serialDriver->println(msg);
    ASSERT_EQ(captured, "world");
}

void serialDriverReadCallbackFires(DriverCallbackTestSuite* suite) {
    std::string captured;
    suite->serialDriver->setReadCallback([&captured](const std::string& msg) {
        captured = msg;
    });

    suite->serialDriver->injectInput("*test\r");
    // Read callback gets the cleaned message (framing stripped)
    ASSERT_EQ(captured, "test");
}

void serialDriverCallbacksCoexistWithStringCallback(DriverCallbackTestSuite* suite) {
    std::string writeCapture;
    std::string readCapture;
    std::string stringCapture;

    suite->serialDriver->setWriteCallback([&writeCapture](const std::string& msg) {
        writeCapture = msg;
    });
    suite->serialDriver->setReadCallback([&readCapture](const std::string& msg) {
        readCapture = msg;
    });
    suite->serialDriver->setStringCallback([&stringCapture](const std::string& msg) {
        stringCapture = msg;
    });

    suite->serialDriver->println("out");
    suite->serialDriver->injectInput("*in\r");

    ASSERT_EQ(writeCapture, "out");
    ASSERT_EQ(readCapture, "in");
    ASSERT_EQ(stringCapture, "in");
}

void displayDriverTextCallbackFires(DriverCallbackTestSuite* suite) {
    std::string captured;
    suite->displayDriver->setTextCallback([&captured](const std::string& text) {
        captured = text;
    });

    suite->displayDriver->drawText("HELLO WORLD");
    ASSERT_EQ(captured, "HELLO WORLD");
}

void displayDriverTextCallbackPositioned(DriverCallbackTestSuite* suite) {
    std::string captured;
    suite->displayDriver->setTextCallback([&captured](const std::string& text) {
        captured = text;
    });

    suite->displayDriver->drawText("POSITIONED", 10, 20);
    ASSERT_EQ(captured, "POSITIONED");
}

void displayDriverTextCallbackMultipleCalls(DriverCallbackTestSuite* suite) {
    std::vector<std::string> captured;
    suite->displayDriver->setTextCallback([&captured](const std::string& text) {
        captured.push_back(text);
    });

    suite->displayDriver->drawText("FIRST");
    suite->displayDriver->drawText("SECOND", 0, 10);
    ASSERT_EQ(captured.size(), 2u);
    ASSERT_EQ(captured[0], "FIRST");
    ASSERT_EQ(captured[1], "SECOND");
}

// ============================================================
// ScriptRunner Test Suite
// ============================================================

#include "cli/cli-script-runner.hpp"

class ScriptRunnerTestSuite : public testing::Test {
public:
    cli::EventLogger eventLogger;
    cli::CommandProcessor commandProcessor;
    std::vector<cli::DeviceInstance> devices;

    void SetUp() override {
        // Create 2 player devices
        devices.push_back(cli::DeviceFactory::createDevice(0, true));   // hunter
        devices.push_back(cli::DeviceFactory::createDevice(1, false));  // bounty

        // Run a few loops to let FetchUserData complete
        for (int i = 0; i < 5; i++) {
            for (auto& d : devices) {
                d.pdn->loop();
                d.game->loop();
            }
        }
        // Skip to Idle state (stateMap index 7 — NOT state ID 8)
        for (auto& d : devices) {
            d.game->skipToState(7);
        }
    }

    void TearDown() override {
        for (auto& d : devices) {
            cli::DeviceFactory::destroyDevice(d);
        }
    }
};

// --- ScriptRunner tests ---

void scriptRunnerParseSkipsComments(ScriptRunnerTestSuite* suite) {
    cli::ScriptRunner runner(suite->devices, suite->commandProcessor, suite->eventLogger);

    runner.loadFromString(
        "# This is a comment\n"
        "\n"
        "tick       # inline comment\n"
        "# Another full-line comment\n"
        "\n"
        "tick 2\n"
    );

    // Only 2 actual commands: "tick" and "tick 2"
    ASSERT_EQ(runner.commandCount(), 2u);
    ASSERT_TRUE(runner.hasMore());
}

void scriptRunnerExecutesCableCommand(ScriptRunnerTestSuite* suite) {
    cli::ScriptRunner runner(suite->devices, suite->commandProcessor, suite->eventLogger);

    runner.loadFromString("cable 0 1\n");

    std::string error = runner.executeAll();
    ASSERT_EQ(error, "");

    // Verify devices are connected through the broker
    int connected = cli::SerialCableBroker::getInstance().getConnectedDevice(0);
    ASSERT_EQ(connected, 1);
}

void scriptRunnerWaitAdvancesClock(ScriptRunnerTestSuite* suite) {
    cli::ScriptRunner runner(suite->devices, suite->commandProcessor, suite->eventLogger);

    unsigned long before = suite->devices[0].clockDriver->milliseconds();

    runner.loadFromString("wait 1000\n");
    std::string error = runner.executeAll();
    ASSERT_EQ(error, "");

    unsigned long after = suite->devices[0].clockDriver->milliseconds();
    // Clock should have advanced by 1000ms
    ASSERT_GE(after - before, 1000u);
}

void scriptRunnerTickRunsLoops(ScriptRunnerTestSuite* suite) {
    cli::ScriptRunner runner(suite->devices, suite->commandProcessor, suite->eventLogger);

    unsigned long before = suite->devices[0].clockDriver->milliseconds();

    runner.loadFromString("tick 5\n");
    std::string error = runner.executeAll();
    ASSERT_EQ(error, "");

    unsigned long after = suite->devices[0].clockDriver->milliseconds();
    // 5 ticks * 33ms = 165ms
    ASSERT_GE(after - before, 165u);
}

void scriptRunnerPressButton(ScriptRunnerTestSuite* suite) {
    cli::ScriptRunner runner(suite->devices, suite->commandProcessor, suite->eventLogger);

    runner.loadFromString("press 0 primary\n");
    std::string error = runner.executeAll();
    ASSERT_EQ(error, "");

    // Verify a BUTTON_PRESS event was logged
    auto events = suite->eventLogger.getByType(cli::EventType::BUTTON_PRESS);
    ASSERT_GE(events.size(), 1u);
    ASSERT_EQ(events[0].deviceIndex, 0);
    ASSERT_EQ(events[0].button, "primary");
}

void scriptRunnerAssertStatePass(ScriptRunnerTestSuite* suite) {
    cli::ScriptRunner runner(suite->devices, suite->commandProcessor, suite->eventLogger);

    // Devices were skipped to Idle in SetUp
    runner.loadFromString("assert-state 0 Idle\n");
    std::string error = runner.executeAll();
    ASSERT_EQ(error, "");
}

void scriptRunnerAssertStateFail(ScriptRunnerTestSuite* suite) {
    cli::ScriptRunner runner(suite->devices, suite->commandProcessor, suite->eventLogger);

    runner.loadFromString("assert-state 0 Sleep\n");
    std::string error = runner.executeAll();
    ASSERT_FALSE(error.empty());
    ASSERT_TRUE(error.find("assert-state failed") != std::string::npos);
    ASSERT_TRUE(error.find("Idle") != std::string::npos);
    ASSERT_TRUE(error.find("Sleep") != std::string::npos);
}

void scriptRunnerAssertTextPass(ScriptRunnerTestSuite* suite) {
    cli::ScriptRunner runner(suite->devices, suite->commandProcessor, suite->eventLogger);

    // Draw some text on device 0's display
    suite->devices[0].displayDriver->drawText("READY TO DUEL");

    runner.loadFromString("assert-text 0 \"READY TO DUEL\"\n");
    std::string error = runner.executeAll();
    ASSERT_EQ(error, "");
}

void scriptRunnerAssertSerialTx(ScriptRunnerTestSuite* suite) {
    cli::ScriptRunner runner(suite->devices, suite->commandProcessor, suite->eventLogger);

    // Log a SERIAL_TX event manually
    cli::Event evt;
    evt.type = cli::EventType::SERIAL_TX;
    evt.deviceIndex = 1;
    evt.message = "cdev:7:6";
    suite->eventLogger.log(evt);

    runner.loadFromString("assert-serial-tx 1 \"cdev:\"\n");
    std::string error = runner.executeAll();
    ASSERT_EQ(error, "");
}

void scriptRunnerAssertNoEvent(ScriptRunnerTestSuite* suite) {
    cli::ScriptRunner runner(suite->devices, suite->commandProcessor, suite->eventLogger);

    // Event logger is empty at start
    runner.loadFromString("assert-no-event SERIAL_TX dev=0 \"xyz\"\n");
    std::string error = runner.executeAll();
    ASSERT_EQ(error, "");
}

void scriptRunnerStopsOnFirstError(ScriptRunnerTestSuite* suite) {
    cli::ScriptRunner runner(suite->devices, suite->commandProcessor, suite->eventLogger);

    runner.loadFromString(
        "tick\n"
        "assert-state 0 Sleep\n"
        "tick\n"
    );

    std::string error = runner.executeAll();
    ASSERT_FALSE(error.empty());
    ASSERT_TRUE(error.find("assert-state failed") != std::string::npos);
    // Script should have stopped after the assertion failure, leaving the third command
    ASSERT_TRUE(runner.hasMore());
}

void scriptRunnerReportsLineNumber(ScriptRunnerTestSuite* suite) {
    cli::ScriptRunner runner(suite->devices, suite->commandProcessor, suite->eventLogger);

    runner.loadFromString(
        "# comment line 1\n"
        "\n"
        "tick\n"
        "assert-state 0 Sleep\n"
    );

    std::string error = runner.executeAll();
    ASSERT_FALSE(error.empty());
    // "assert-state 0 Sleep" is on line 4 of the original text
    ASSERT_TRUE(error.find("Line 4") != std::string::npos);
}

// ============================================================
// Feature A/B/E Validation Tests
// ============================================================

#include "game/challenge-states.hpp"
#include "game/signal-echo/signal-echo-states.hpp"
#include "game/signal-echo/signal-echo-resources.hpp"

// RAII guard to save/restore global state (g_logger, SimpleTimer clock).
// Ensures globals are restored even when ASSERT_ macros abort the test function.
struct ScopedGlobals {
    LoggerInterface* prevLogger;
    PlatformClock* prevClock;
    NativeLoggerDriver tempLogger;

    ScopedGlobals(const std::string& name, PlatformClock* clock) :
        prevLogger(g_logger),
        prevClock(SimpleTimer::getPlatformClock()),
        tempLogger(name)
    {
        tempLogger.setSuppressOutput(true);
        g_logger = &tempLogger;
        SimpleTimer::setPlatformClock(clock);
    }

    ~ScopedGlobals() {
        SimpleTimer::setPlatformClock(prevClock);
        g_logger = prevLogger;
    }
};

/*
 * Feature A: NPC broadcasts cdev message, player receives it.
 *
 * This test spawns an NPC challenge device, manually attaches
 * event logger callbacks (since ScriptRunnerTestSuite's SetUp
 * only hooks the original 2 player devices), cables the NPC to
 * the player, advances time past the broadcast interval, and
 * asserts that the cdev message was sent by the NPC and received
 * by the player.
 */
void featureA_NpcBroadcastsCdev(ScriptRunnerTestSuite* suite) {
    ScopedGlobals globals("featureA_logger", suite->devices[0].clockDriver);

    // Spawn NPC challenge device at index 2
    suite->devices.push_back(
        cli::DeviceFactory::createChallengeDevice(2, GameType::SIGNAL_ECHO));

    // Attach write callback on NPC's serialOutDriver to log SERIAL_TX events
    suite->devices[2].serialOutDriver->setWriteCallback(
        [suite](const std::string& msg) {
            cli::Event evt;
            evt.type = cli::EventType::SERIAL_TX;
            evt.deviceIndex = 2;
            evt.message = msg;
            suite->eventLogger.log(evt);
        });

    // Attach read callback on Player's serialInDriver to log SERIAL_RX events.
    // The NPC (hunter) and Player 0 (hunter) are same-role, so
    // NPC.output -> Player.input (auxiliary jack).
    suite->devices[0].serialInDriver->setReadCallback(
        [suite](const std::string& msg) {
            cli::Event evt;
            evt.type = cli::EventType::SERIAL_RX;
            evt.deviceIndex = 0;
            evt.message = msg;
            suite->eventLogger.log(evt);
        });

    // Connect player 0 to NPC 2 via serial cable
    cli::SerialCableBroker::getInstance().connect(0, 2);

    // Run ticks: advance clocks past NPC broadcast interval (500ms)
    // and run loop iterations to process broadcasts and serial transfers.
    for (int i = 0; i < 30; i++) {
        for (auto& dev : suite->devices) {
            dev.clockDriver->advance(33);
        }
        NativePeerBroker::getInstance().deliverPackets();
        cli::SerialCableBroker::getInstance().transferData();
        for (auto& dev : suite->devices) {
            dev.pdn->loop();
            dev.game->loop();
        }
    }

    // NPC should have sent a cdev message
    ASSERT_TRUE(suite->eventLogger.hasEvent(cli::EventType::SERIAL_TX, "cdev:"))
        << "NPC should broadcast cdev message";

    // Player should have received the cdev message on its input jack
    auto rxEvents = suite->eventLogger.getByTypeAndDevice(cli::EventType::SERIAL_RX, 0);
    bool foundCdev = false;
    for (const auto& e : rxEvents) {
        if (e.message.find("cdev:") != std::string::npos) {
            foundCdev = true;
            break;
        }
    }
    ASSERT_TRUE(foundCdev) << "Player should receive cdev from NPC";
}

/*
 * Feature A: Verify cdev message format is "cdev:<gameType>:<konamiButton>".
 *
 * For Signal Echo, game type is 7 and konami button is START (6),
 * so the message should be "cdev:7:6".
 */
void featureA_CdevMessageFormat(ScriptRunnerTestSuite* suite) {
    ScopedGlobals globals("featureA_fmt_logger", suite->devices[0].clockDriver);

    // Spawn NPC
    suite->devices.push_back(
        cli::DeviceFactory::createChallengeDevice(2, GameType::SIGNAL_ECHO));

    // Capture all TX messages from the NPC
    std::vector<std::string> npcTxMessages;
    suite->devices[2].serialOutDriver->setWriteCallback(
        [&npcTxMessages](const std::string& msg) {
            npcTxMessages.push_back(msg);
        });

    // Run ticks past broadcast interval
    for (int i = 0; i < 30; i++) {
        for (auto& dev : suite->devices) {
            dev.clockDriver->advance(33);
        }
        for (auto& dev : suite->devices) {
            dev.pdn->loop();
            dev.game->loop();
        }
    }

    // Find the cdev message and verify its exact format
    bool foundExactFormat = false;
    for (const auto& msg : npcTxMessages) {
        if (msg.rfind("cdev:", 0) == 0) {
            // Parse using the protocol function
            GameType parsedGame;
            KonamiButton parsedReward;
            bool parsed = parseCdevMessage(msg, parsedGame, parsedReward);
            ASSERT_TRUE(parsed) << "cdev message should be parseable: " << msg;
            ASSERT_EQ(parsedGame, GameType::SIGNAL_ECHO)
                << "Game type should be SIGNAL_ECHO (7)";
            ASSERT_EQ(parsedReward, KonamiButton::START)
                << "Konami button should be START (6)";
            ASSERT_EQ(msg, "cdev:7:6")
                << "Exact message format should be cdev:7:6";
            foundExactFormat = true;
            break;
        }
    }
    ASSERT_TRUE(foundExactFormat)
        << "NPC should have broadcast a cdev message";
}

/*
 * Feature B: Play through Signal Echo standalone and win.
 *
 * Creates a standalone game device, skips to PlayerInput,
 * reads the generated sequence, presses the correct buttons,
 * and verifies the outcome is WON.
 */
void featureB_SignalEchoStandaloneWin(ScriptRunnerTestSuite* suite) {
    // Create a standalone Signal Echo game device at index 2
    suite->devices.push_back(
        cli::DeviceFactory::createGameDevice(2, "signal-echo"));

    ScopedGlobals globals("featureB_win_logger", suite->devices[2].clockDriver);

    auto& dev = suite->devices[2];
    SignalEcho* game = dynamic_cast<SignalEcho*>(dev.game);
    ASSERT_NE(game, nullptr) << "Game should be a SignalEcho instance";

    // Configure for a quick test: 1 round, short sequence, deterministic seed
    game->getConfig().numSequences = 1;
    game->getConfig().sequenceLength = 3;
    game->getConfig().displaySpeedMs = 10;
    game->getConfig().rngSeed = 42;
    game->getConfig().allowedMistakes = 3;

    // Set a known sequence and skip to PlayerInput (state map index 2)
    game->getSession().currentSequence = {true, false, true};
    game->getSession().currentRound = 0;
    game->getSession().inputIndex = 0;
    game->skipToState(2);  // EchoPlayerInput

    // Run one loop to let the state mount and register button callbacks
    dev.pdn->loop();
    dev.game->loop();

    ASSERT_EQ(game->getCurrentState()->getStateId(), ECHO_PLAYER_INPUT);

    // Press correct buttons matching the sequence: UP, DOWN, UP
    // UP = primary button, DOWN = secondary button
    dev.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);   // true = UP
    dev.pdn->loop();
    dev.game->loop();

    dev.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK); // false = DOWN
    dev.pdn->loop();
    dev.game->loop();

    dev.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);   // true = UP
    dev.pdn->loop();
    dev.game->loop();

    // Process through Evaluate to Win
    for (int i = 0; i < 5; i++) {
        dev.pdn->loop();
        dev.game->loop();
    }

    // Verify the outcome is WON
    const MiniGameOutcome& outcome = game->getMiniGameOutcome();
    ASSERT_EQ(outcome.result, MiniGameResult::WON)
        << "Player should win after entering all correct inputs";
}

/*
 * Feature B: Play through Signal Echo standalone and lose.
 *
 * Creates a standalone game device, skips to PlayerInput,
 * presses wrong buttons until mistakes exceed the allowed count,
 * and verifies the outcome is LOST.
 */
void featureB_SignalEchoStandaloneLose(ScriptRunnerTestSuite* suite) {
    // Create a standalone Signal Echo game device at index 2
    suite->devices.push_back(
        cli::DeviceFactory::createGameDevice(2, "signal-echo"));

    ScopedGlobals globals("featureB_lose_logger", suite->devices[2].clockDriver);

    auto& dev = suite->devices[2];
    SignalEcho* game = dynamic_cast<SignalEcho*>(dev.game);
    ASSERT_NE(game, nullptr) << "Game should be a SignalEcho instance";

    // Configure: allow 1 mistake, sequence of all UPs
    game->getConfig().numSequences = 3;
    game->getConfig().sequenceLength = 4;
    game->getConfig().displaySpeedMs = 10;
    game->getConfig().allowedMistakes = 1;

    // Set a sequence of all UPs so pressing DOWN is always wrong
    game->getSession().currentSequence = {true, true, true, true};
    game->getSession().currentRound = 0;
    game->getSession().inputIndex = 0;
    game->getSession().mistakes = 0;
    game->skipToState(2);  // EchoPlayerInput

    // Run one loop to mount
    dev.pdn->loop();
    dev.game->loop();

    ASSERT_EQ(game->getCurrentState()->getStateId(), ECHO_PLAYER_INPUT);

    // Press wrong button (DOWN when UP expected) twice
    // First mistake brings us to allowedMistakes (1), second exceeds it
    dev.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);  // Wrong
    dev.pdn->loop();
    dev.game->loop();
    ASSERT_EQ(game->getSession().mistakes, 1);

    dev.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);  // Wrong again, exceeds
    // Process through Evaluate to Lose
    for (int i = 0; i < 5; i++) {
        dev.pdn->loop();
        dev.game->loop();
    }

    // Verify the outcome is LOST
    const MiniGameOutcome& outcome = game->getMiniGameOutcome();
    ASSERT_EQ(outcome.result, MiniGameResult::LOST)
        << "Player should lose after exceeding allowed mistakes";
}

/*
 * Feature E: Konami progress tracking via ProgressManager.
 *
 * Creates a player device, unlocks a konami button, saves
 * progress via ProgressManager, clears the player's progress,
 * loads it back from NVS, and verifies the unlock persisted.
 */
void featureE_KonamiProgressTracking(ScriptRunnerTestSuite* suite) {
    // Use device 0 (hunter) which already has player + progressManager
    auto& dev = suite->devices[0];
    ASSERT_NE(dev.player, nullptr) << "Player should exist on device 0";
    ASSERT_NE(dev.progressManager, nullptr) << "ProgressManager should exist on device 0";

    // Verify the button is not yet unlocked
    ASSERT_FALSE(dev.player->hasUnlockedButton(KonamiButton::START))
        << "START should not be unlocked initially";

    // Unlock the START button (Signal Echo reward)
    dev.player->unlockKonamiButton(KonamiButton::START);
    ASSERT_TRUE(dev.player->hasUnlockedButton(KonamiButton::START))
        << "START should be unlocked after unlockKonamiButton()";

    // Save progress to NVS
    dev.progressManager->saveProgress();

    // Verify NVS has the data
    ASSERT_TRUE(dev.progressManager->hasUnsyncedProgress())
        << "Progress should be marked as unsynced after save";

    // Remember the original progress bitmask
    uint8_t savedProgress = dev.player->getKonamiProgress();
    ASSERT_NE(savedProgress, 0u) << "Saved progress should be non-zero";

    // Clear the player's in-memory progress to simulate a fresh load
    dev.player->setKonamiProgress(0);
    ASSERT_FALSE(dev.player->hasUnlockedButton(KonamiButton::START))
        << "START should be cleared after setKonamiProgress(0)";

    // Load progress back from NVS
    dev.progressManager->loadProgress();

    // Verify the unlock was restored
    ASSERT_TRUE(dev.player->hasUnlockedButton(KonamiButton::START))
        << "START should be restored after loadProgress()";
    ASSERT_EQ(dev.player->getKonamiProgress(), savedProgress)
        << "Full progress bitmask should match what was saved";
}
