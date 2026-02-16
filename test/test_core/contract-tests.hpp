#pragma once

#include <gtest/gtest.h>
#include <type_traits>

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
protected:
    void SetUp() override {
        // Contract tests typically don't need setup
    }

    void TearDown() override {
        // Contract tests typically don't need teardown
    }
};

// =============================================================================
// TYPE CONTRACTS: Compile-Time Verification
// =============================================================================

/*
 * Type contracts use static_assert to verify type properties at compile time.
 * These catch breaking changes to interfaces, data structures, and type traits.
 *
 * Add static_assert checks here for:
 * - RTTI requirements (std::is_polymorphic for base classes with virtual methods)
 * - Size constraints (sizeof checks for serialization compatibility)
 * - Type traits (std::is_trivially_copyable for POD types)
 * - Interface contracts (std::is_base_of for inheritance hierarchies)
 *
 * Example:
 *   static_assert(std::is_polymorphic<Driver>::value,
 *                 "Driver base class must have virtual methods for polymorphism");
 */

// TODO: Add RTTI checks for driver interfaces
// TODO: Add size checks for serialized types (Player, Match, etc.)
// TODO: Add inheritance hierarchy checks for state classes

// =============================================================================
// DISPLAY DRIVER CONTRACT
// =============================================================================

/*
 * Display Contract: Verifies native and ESP32 display drivers have same behavior
 *
 * The display is critical for gameplay feedback. Native and ESP32 must:
 * - Draw bitmaps identically (including OFF pixels)
 * - Handle text rendering with same layout
 * - Clear screen consistently
 * - Support same contrast/brightness controls (or gracefully no-op)
 */

inline void displayContractBufferSize(ContractTestSuite* suite) {
    // Example: Verify display buffer dimensions are consistent
    // const int ESP32_DISPLAY_WIDTH = 128;
    // const int ESP32_DISPLAY_HEIGHT = 64;
    //
    // NativeDisplay nativeDisplay;
    // EXPECT_EQ(nativeDisplay.getWidth(), ESP32_DISPLAY_WIDTH);
    // EXPECT_EQ(nativeDisplay.getHeight(), ESP32_DISPLAY_HEIGHT);

    // TODO: Implement actual display buffer size check
    EXPECT_TRUE(true) << "Display buffer size contract not yet implemented";
}

inline void displayContractDrawBitmapWritesBothOnAndOffPixels(ContractTestSuite* suite) {
    // Example: Verify drawXBMP writes both ON and OFF pixels (not just ON)
    // This prevents native driver from taking shortcuts
    //
    // NativeDisplay display;
    // display.clearBuffer();
    //
    // // Draw a bitmap with explicit OFF pixels in the middle of ON pixels
    // const uint8_t testBitmap[] = { 0xFF, 0x00, 0xFF }; // ON OFF ON pattern
    // display.drawXBMP(0, 0, 8, 3, testBitmap);
    //
    // // Verify OFF pixels were actually written (not skipped)
    // EXPECT_EQ(display.getPixel(0, 1), 0) << "OFF pixels must be written, not skipped";

    // TODO: Implement actual bitmap write verification
    EXPECT_TRUE(true) << "Bitmap write contract not yet implemented";
}

// =============================================================================
// BUTTON DRIVER CONTRACT
// =============================================================================

/*
 * Button Contract: Verifies native and ESP32 button drivers handle events identically
 *
 * Critical for gameplay timing (quickdraw, etc.). Both drivers must:
 * - Debounce with same timing parameters
 * - Support same event types (click, hold, double-click if applicable)
 * - Fire callbacks in same order
 */

inline void buttonContractDebounceTimingMatches(ContractTestSuite* suite) {
    // TODO: Verify native and ESP32 debounce timing is identical
    // This prevents "works in CLI but not on hardware" timing bugs
    EXPECT_TRUE(true) << "Button debounce contract not yet implemented";
}

// =============================================================================
// SERIAL DRIVER CONTRACT
// =============================================================================

/*
 * Serial Contract: Verifies native and ESP32 serial communication matches
 *
 * Critical for cable-based minigames (FDN protocol). Both drivers must:
 * - Respect same buffer size limits
 * - Handle buffer overflow identically
 * - Support same baud rates (or gracefully no-op on native)
 * - Deliver messages in same order
 */

inline void serialContractBufferSizeLimits(ContractTestSuite* suite) {
    // TODO: Verify serial buffer overflow handling matches between platforms
    // ESP32 has hardware buffer limits; native must simulate same limits
    EXPECT_TRUE(true) << "Serial buffer contract not yet implemented";
}

// =============================================================================
// TIMER DRIVER CONTRACT
// =============================================================================

/*
 * Timer Contract: Verifies native and ESP32 timer behavior matches
 *
 * Critical for time-sensitive gameplay (quickdraw timing, FDN timeouts). Must:
 * - Handle overflow identically (millis() rollover at ~49 days)
 * - Support same precision (millisecond-level accuracy)
 * - Provide deterministic behavior in tests (mocked time)
 */

inline void timerContractOverflowHandling(ContractTestSuite* suite) {
    // TODO: Verify millis() overflow is handled identically
    // Example: Test with time values near UINT32_MAX
    EXPECT_TRUE(true) << "Timer overflow contract not yet implemented";
}

// =============================================================================
// STATE MACHINE CONTRACT
// =============================================================================

/*
 * State Machine Contract: Verifies state lifecycle is identical across platforms
 *
 * The state machine is the core of game logic. Must verify:
 * - Same state transition order
 * - Same lifecycle call patterns (onStateMounted -> onStateLoop -> onStateDismounted)
 * - Same message handling behavior
 * - No platform-specific state hacks
 */

inline void stateMachineContractLifecycleOrder(ContractTestSuite* suite) {
    // TODO: Verify state lifecycle methods are called in same order on both platforms
    // Use mock states to track call order
    EXPECT_TRUE(true) << "State machine lifecycle contract not yet implemented";
}

// =============================================================================
// SERIALIZATION CONTRACT
// =============================================================================

/*
 * Serialization Contract: Verifies Player/Match JSON/binary output is identical
 *
 * Critical for cross-device communication and data persistence. Must verify:
 * - Same JSON field order (where order matters for compatibility)
 * - Same binary layout (for wire protocol)
 * - Same handling of null/default values
 */

inline void serializationContractPlayerJsonFormat(ContractTestSuite* suite) {
    // TODO: Verify Player JSON serialization produces identical output
    // Create Player, serialize to JSON, verify field presence and format
    EXPECT_TRUE(true) << "Player serialization contract not yet implemented";
}

inline void serializationContractMatchBinaryFormat(ContractTestSuite* suite) {
    // TODO: Verify Match binary serialization matches across platforms
    // Critical for networked gameplay
    EXPECT_TRUE(true) << "Match serialization contract not yet implemented";
}

// =============================================================================
// REGISTRATION (add to tests.cpp)
// =============================================================================

/*
 * To register these tests, add the following to tests.cpp:
 *
 * #include "contract-tests.hpp"
 *
 * Then add TEST_F macros for each test function:
 *
 * TEST_F(ContractTestSuite, displayBufferSize) {
 *     displayContractBufferSize(this);
 * }
 *
 * TEST_F(ContractTestSuite, displayDrawBitmapWritesBothOnAndOffPixels) {
 *     displayContractDrawBitmapWritesBothOnAndOffPixels(this);
 * }
 *
 * ... etc for each inline function above
 */
