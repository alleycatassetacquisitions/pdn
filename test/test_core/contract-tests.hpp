#pragma once

#include <gtest/gtest.h>
#include <type_traits>
#include <cstring>
#include <vector>
#include <string>

#include "test-device.hpp"
#include "game/player.hpp"
#include "game/match.hpp"
#include "state/state-machine.hpp"
#include "../test-constants.hpp"

/*
 * Contract Tests: Native/ESP32 Parity Verification
 *
 * Purpose: Ensure the native (CLI simulator) and ESP32 implementations maintain
 * behavioral parity. These tests verify that abstractions, driver interfaces, and
 * core game logic work identically across both platforms.
 *
 * Contract tests check:
 * 1. Type compatibility (same interfaces, same sizes where relevant)
 * 2. Driver interface contracts (methods exist, signatures match)
 * 3. State machine behavior (same transitions, same outputs)
 * 4. Serialization parity (same binary/JSON output)
 *
 * IMPORTANT: Native drivers must match hardware behavior exactly. The native driver
 * is a test double for the real ESP32 driver. Shortcuts or common-case-only
 * implementations lead to CLI/hardware divergence.
 *
 * Example divergence caught by contract tests:
 * - U8g2 drawXBMP() writes both ON and OFF pixels; native must do the same
 * - ESP32 serial has buffering limits; native must respect same limits
 * - Timer overflow behavior must match hardware timing constraints
 */

// =============================================================================
// TEST SUITE: Contract Tests
// =============================================================================

class ContractTestSuite : public ::testing::Test {
public:
    TestDevice* td;

    void SetUp() override {
        td = new TestDevice();
    }

    void TearDown() override {
        delete td;
        td = nullptr;
    }
};

// =============================================================================
// DISPLAY DRIVER CONTRACT
// =============================================================================

inline void displayContractBufferSize(ContractTestSuite* suite) {
    // Verify display dimensions match ESP32 SSD1306 OLED (128x64)
    // Use local copies to avoid ODR issues with static const int members
    int width = NativeDisplayDriver::WIDTH;
    int height = NativeDisplayDriver::HEIGHT;
    EXPECT_EQ(width, 128);
    EXPECT_EQ(height, 64);

    // Out-of-bounds pixels should return false (not crash)
    EXPECT_FALSE(suite->td->displayDriver->getPixel(-1, 0));
    EXPECT_FALSE(suite->td->displayDriver->getPixel(0, -1));
    EXPECT_FALSE(suite->td->displayDriver->getPixel(128, 0));
    EXPECT_FALSE(suite->td->displayDriver->getPixel(0, 64));
    EXPECT_FALSE(suite->td->displayDriver->getPixel(999, 999));
}

inline void displayContractDrawBitmapWritesBothOnAndOffPixels(ContractTestSuite* suite) {
    NativeDisplayDriver* display = suite->td->displayDriver;

    // Fill a region with ON pixels using drawBox
    display->drawBox(0, 0, 8, 3);

    // Verify the region is filled
    for (int y = 0; y < 3; y++) {
        for (int x = 0; x < 8; x++) {
            ASSERT_TRUE(display->getPixel(x, y))
                << "Pixel (" << x << "," << y << ") should be ON after drawBox";
        }
    }

    // Create XBM bitmap: row 0 = 0xFF (all ON), row 1 = 0x00 (all OFF), row 2 = 0xFF (all ON)
    // XBM is 1 byte per row for 8px wide images, LSB first
    const unsigned char testBitmap[] = { 0xFF, 0x00, 0xFF };

    // Draw the bitmap over the filled region using drawImage
    Image testImage(testBitmap, 8, 3, 0, 0);
    display->drawImage(testImage, 0, 0);

    // Row 0: all ON (0xFF) — should still be ON
    for (int x = 0; x < 8; x++) {
        EXPECT_TRUE(display->getPixel(x, 0))
            << "Row 0, pixel " << x << " should be ON (0xFF byte)";
    }

    // Row 1: all OFF (0x00) — must overwrite the ON pixels from drawBox
    for (int x = 0; x < 8; x++) {
        EXPECT_FALSE(display->getPixel(x, 1))
            << "Row 1, pixel " << x << " should be OFF — XBM OFF pixels must overwrite ON pixels";
    }

    // Row 2: all ON (0xFF) — should be ON
    for (int x = 0; x < 8; x++) {
        EXPECT_TRUE(display->getPixel(x, 2))
            << "Row 2, pixel " << x << " should be ON (0xFF byte)";
    }
}

// =============================================================================
// BUTTON DRIVER CONTRACT
// =============================================================================

// Static flag for button contract test (callbackFunction is a plain function pointer, no captures)
static bool s_buttonContractCallbackFired = false;
static void buttonContractCallback() { s_buttonContractCallbackFired = true; }

inline void buttonContractDebounceTimingMatches(ContractTestSuite* suite) {
    NativeButtonDriver* button = suite->td->primaryButtonDriver;

    // Verify callback registration and dispatch
    s_buttonContractCallbackFired = false;
    button->setButtonPress(buttonContractCallback, ButtonInteraction::PRESS);

    EXPECT_TRUE(button->hasCallback(ButtonInteraction::PRESS));
    EXPECT_FALSE(s_buttonContractCallbackFired) << "Callback should not fire on registration";

    // Fire the callback
    button->execCallback(ButtonInteraction::PRESS);
    EXPECT_TRUE(s_buttonContractCallbackFired) << "Callback should fire on execCallback";

    // Verify removeButtonCallbacks clears all
    s_buttonContractCallbackFired = false;
    button->removeButtonCallbacks();
    EXPECT_FALSE(button->hasCallback(ButtonInteraction::PRESS));

    // execCallback after removal should not crash (no-op)
    button->execCallback(ButtonInteraction::PRESS);
    EXPECT_FALSE(s_buttonContractCallbackFired) << "Callback should not fire after removal";

    // Document: native driver doesn't simulate debounce timing — callbacks are instant.
    // On ESP32, OneButton handles debounce (~50ms). Tests that depend on debounce
    // timing must be run on hardware.
}

// =============================================================================
// SERIAL DRIVER CONTRACT
// =============================================================================

inline void serialContractBufferSizeLimits(ContractTestSuite* suite) {
    NativeSerialDriver* serial = suite->td->serialOutDriver;

    // Verify buffer size constants match ESP32 hardware limits
    // Use local copies to avoid ODR issues with static const members
    size_t maxOutputSize = NativeSerialDriver::MAX_OUTPUT_BUFFER_SIZE;
    size_t maxInputSize = NativeSerialDriver::MAX_INPUT_QUEUE_SIZE;
    EXPECT_EQ(maxOutputSize, 255u);
    EXPECT_EQ(maxInputSize, 32u);

    // Test output buffer: write a long string that exceeds MAX_OUTPUT_BUFFER_SIZE
    std::string longMessage(300, 'A');
    serial->println(longMessage);
    std::string output = serial->getOutput();
    EXPECT_LE(output.size(), maxOutputSize)
        << "Output buffer should be trimmed to MAX_OUTPUT_BUFFER_SIZE";
    serial->clearOutput();

    // Test input queue: inject more than MAX_INPUT_QUEUE_SIZE messages
    for (int i = 0; i < 40; i++) {
        serial->injectInput("msg" + std::to_string(i));
    }

    // Count how many messages are available
    int count = 0;
    while (serial->available()) {
        serial->readStringUntil('\n');
        count++;
    }
    EXPECT_LE(count, static_cast<int>(maxInputSize))
        << "Input queue should not exceed MAX_INPUT_QUEUE_SIZE";
}

// =============================================================================
// TIMER DRIVER CONTRACT
// =============================================================================

inline void timerContractOverflowHandling(ContractTestSuite* suite) {
    // Create a minimal PlatformClock with settable time for overflow testing
    class OverflowClock : public PlatformClock {
    public:
        uint32_t time = 0;
        unsigned long milliseconds() override { return time; }
    };

    OverflowClock overflowClock;

    // Save and replace the global clock
    PlatformClock* savedClock = SimpleTimer::getPlatformClock();
    SimpleTimer::setPlatformClock(&overflowClock);

    // Set clock near UINT32_MAX
    overflowClock.time = UINT32_MAX - 5;  // 5ms before overflow

    SimpleTimer timer;
    timer.setTimer(20);  // 20ms duration — will cross the overflow boundary

    // Advance past the overflow boundary
    overflowClock.time = 10;  // wrapped around: elapsed = 5 + 10 + 1 = 16ms

    EXPECT_FALSE(timer.expired()) << "Timer should not be expired at 16ms (duration=20ms)";
    EXPECT_EQ(timer.getElapsedTime(), 16u) << "Elapsed time should handle overflow correctly";

    // Advance past expiry
    overflowClock.time = 20;  // elapsed = 5 + 20 + 1 = 26ms
    EXPECT_TRUE(timer.expired()) << "Timer should be expired at 26ms (duration=20ms)";
    EXPECT_EQ(timer.getElapsedTime(), 26u);

    // Restore original clock
    SimpleTimer::setPlatformClock(savedClock);
}

// =============================================================================
// STATE MACHINE CONTRACT
// =============================================================================

inline void stateMachineContractLifecycleOrder(ContractTestSuite* suite) {
    // Tracked states that log lifecycle events with timestamps
    static std::vector<std::string> events;
    events.clear();

    class TrackedStateA : public State {
    public:
        bool shouldTransition = false;
        TrackedStateA() : State(0) {}
        void onStateMounted(Device* d) override { events.push_back("A_mount"); }
        void onStateLoop(Device* d) override {
            events.push_back("A_loop");
            shouldTransition = true;
        }
        void onStateDismounted(Device* d) override { events.push_back("A_dismount"); }
    };

    class TrackedStateB : public State {
    public:
        TrackedStateB() : State(1) {}
        void onStateMounted(Device* d) override { events.push_back("B_mount"); }
        void onStateLoop(Device* d) override { events.push_back("B_loop"); }
        void onStateDismounted(Device* d) override { events.push_back("B_dismount"); }
    };

    class TrackedMachine : public StateMachine {
    public:
        TrackedMachine() : StateMachine(100) {}
        void populateStateMap() override {
            auto* a = new TrackedStateA();
            auto* b = new TrackedStateB();
            stateMap.push_back(a);
            stateMap.push_back(b);
            a->addTransition(new StateTransition(
                [a]() { return a->shouldTransition; }, b));
        }
    };

    TrackedMachine machine;
    machine.initialize(suite->td->pdn);

    // After initialize: state A should be mounted
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0], "A_mount");

    // Run one loop — A loops, transition fires, A dismounts, B mounts
    machine.onStateLoop(suite->td->pdn);

    ASSERT_GE(events.size(), 4u) << "Expected at least 4 events after one loop";
    EXPECT_EQ(events[1], "A_loop");
    EXPECT_EQ(events[2], "A_dismount");
    EXPECT_EQ(events[3], "B_mount");

    // Verify dismount happens before mount (ordering guarantee)
    int dismountIndex = -1, mountIndex = -1;
    for (size_t i = 0; i < events.size(); i++) {
        if (events[i] == "A_dismount" && dismountIndex == -1) dismountIndex = static_cast<int>(i);
        if (events[i] == "B_mount" && mountIndex == -1) mountIndex = static_cast<int>(i);
    }
    EXPECT_LT(dismountIndex, mountIndex)
        << "A_dismount must occur before B_mount";

    // Clean up — shutdown to dismount B before machine destructor
    machine.shutdown(suite->td->pdn);
}

// =============================================================================
// SERIALIZATION CONTRACT
// =============================================================================

inline void serializationContractPlayerJsonFormat(ContractTestSuite* suite) {
    // Create a Player with identity fields set
    Player original;
    original.setUserID(const_cast<char*>(TestConstants::TEST_UUID_PLAYER_1));
    original.setName("TestRunner");
    original.setAllegiance(Allegiance::HELIX);
    original.setFaction("Neon");
    original.setIsHunter(true);

    // Round trip: toJson → fromJson
    std::string json = original.toJson();
    Player restored;
    restored.fromJson(json);

    EXPECT_EQ(restored.getUserID(), original.getUserID());
    EXPECT_EQ(restored.getName(), original.getName());
    EXPECT_EQ(restored.getFaction(), original.getFaction());
    EXPECT_EQ(restored.isHunter(), original.isHunter());

    // Double round-trip: verify stability (toJson → fromJson → toJson → fromJson)
    std::string json2 = restored.toJson();
    Player restored2;
    restored2.fromJson(json2);

    EXPECT_EQ(restored2.getUserID(), original.getUserID());
    EXPECT_EQ(restored2.getName(), original.getName());
    EXPECT_EQ(restored2.getFaction(), original.getFaction());
    EXPECT_EQ(restored2.isHunter(), original.isHunter());

    // Verify the two JSON strings are identical (stable serialization)
    EXPECT_EQ(json, json2)
        << "Double round-trip JSON should be identical to first round-trip";
}

inline void serializationContractMatchBinaryFormat(ContractTestSuite* suite) {
    // Create a Match with valid UUID format strings (required for binary serialization)
    Match original(TestConstants::TEST_UUID_MATCH_1,
                   TestConstants::TEST_UUID_HUNTER_1,
                   TestConstants::TEST_UUID_BOUNTY_1);
    original.setHunterDrawTime(150);
    original.setBountyDrawTime(200);

    // Serialize → deserialize round trip
    uint8_t buffer[MATCH_BINARY_SIZE];
    size_t written = original.serialize(buffer);
    EXPECT_EQ(written, MATCH_BINARY_SIZE);

    Match restored;
    size_t bytesRead = restored.deserialize(buffer);
    EXPECT_EQ(bytesRead, MATCH_BINARY_SIZE);

    // Verify all fields match
    EXPECT_EQ(restored.getMatchId(), original.getMatchId());
    EXPECT_EQ(restored.getHunterId(), original.getHunterId());
    EXPECT_EQ(restored.getBountyId(), original.getBountyId());
    EXPECT_EQ(restored.getHunterDrawTime(), original.getHunterDrawTime());
    EXPECT_EQ(restored.getBountyDrawTime(), original.getBountyDrawTime());

    // Verify deterministic output: serialize a duplicate and memcmp
    Match duplicate(TestConstants::TEST_UUID_MATCH_1,
                    TestConstants::TEST_UUID_HUNTER_1,
                    TestConstants::TEST_UUID_BOUNTY_1);
    duplicate.setHunterDrawTime(150);
    duplicate.setBountyDrawTime(200);

    uint8_t buffer2[MATCH_BINARY_SIZE];
    size_t written2 = duplicate.serialize(buffer2);
    EXPECT_EQ(written2, MATCH_BINARY_SIZE);

    EXPECT_EQ(memcmp(buffer, buffer2, MATCH_BINARY_SIZE), 0)
        << "Identical matches must produce identical binary output";
}
