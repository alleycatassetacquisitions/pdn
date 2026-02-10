#pragma once

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
