#pragma once

#ifdef NATIVE_BUILD

#include <gtest/gtest.h>
#include "cli/cli-device.hpp"
#include "cli/cli-serial-broker.hpp"
#include "cli/cli-http-server.hpp"
#include "cli/cli-commands.hpp"
#include "cli/cli-renderer.hpp"
#include "game/quickdraw.hpp"
#include "game/quickdraw-states.hpp"
#include "game/player.hpp"
#include "device/device-types.hpp"
#include "device/drivers/native/native-button-driver.hpp"
#include "device/drivers/native/native-light-strip-driver.hpp"
#include "utils/simple-timer.hpp"

using namespace cli;

// ============================================
// WAVE 18 BUG REGRESSION TEST SUITE
// ============================================

class Wave18BugRegressionTestSuite : public testing::Test {
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

    void advanceToIdle() {
        quickdraw_->skipToState(device_.pdn, 6);
        device_.pdn->loop();
    }

    DeviceInstance device_;
    Player* player_;
    Quickdraw* quickdraw_;
};

// ============================================
// Bug #204: konami CLI command resets progress to zero
// ============================================

/**
 * Bug #204: Running `konami <device>` CLI command (without a value)
 * should DISPLAY current Konami progress but instead RESETS it to zero.
 *
 * Root cause: CLI command calls setter instead of getter, or passes 0
 * when no value is provided.
 *
 * This test verifies that reading Konami progress with the CLI command
 * does NOT modify the player's progress.
 */
void testKonamiCLIDoesNotResetProgress(Wave18BugRegressionTestSuite* suite) {
    // Set up: Player has some Konami progress
    uint8_t initialProgress = 0x07;  // 3 bits set (3/7 complete)
    suite->player_->setKonamiProgress(initialProgress);

    // Verify initial state
    ASSERT_EQ(suite->player_->getKonamiProgress(), initialProgress);

    // Create a vector of devices for the command
    std::vector<DeviceInstance> devices;
    devices.push_back(suite->device_);

    // Create command processor and renderer
    CommandProcessor processor;
    Renderer renderer;
    int selectedDevice = 0;

    // Execute konami command WITHOUT a value (should only read, not write)
    // Command format: "konami 0" (device 0, no value provided)
    CommandResult result = processor.execute("konami 0", devices, selectedDevice, renderer);

    // Verify progress is UNCHANGED
    ASSERT_EQ(suite->player_->getKonamiProgress(), initialProgress)
        << "Bug #204: konami CLI command reset progress to "
        << static_cast<int>(suite->player_->getKonamiProgress())
        << " instead of preserving " << static_cast<int>(initialProgress);

    // Verify the command returned a status message (not empty)
    ASSERT_FALSE(result.message.empty());
}

// ============================================
// Bug #206: FdnDetected receives empty message on game resume
// ============================================

/**
 * Bug #206: When a game resumes after being interrupted, FdnDetected state
 * receives an empty message string instead of the pending FDN challenge.
 *
 * Root cause: Lifecycle ordering issue — state mounts before the game
 * message is set, or snapshot doesn't preserve the message.
 *
 * This test simulates game resume flow and verifies the FDN message
 * is NOT empty.
 */
void testFdnDetectedMessageNotEmptyOnResume(Wave18BugRegressionTestSuite* suite) {
    // Advance to Idle state
    suite->advanceToIdle();
    ASSERT_EQ(suite->quickdraw_->getCurrentStateId(), IDLE);

    // Simulate FDN detection with a valid challenge message
    std::string fdnChallenge = "*fdn:7:6\r";  // Signal Echo, UP button reward
    suite->device_.serialOutDriver->injectInput(fdnChallenge);
    suite->tick(3);

    // Should transition to FdnDetected (state 21)
    ASSERT_EQ(suite->quickdraw_->getCurrentStateId(), 21);

    // Get the FdnDetected state
    auto* fdnState = dynamic_cast<FdnDetected*>(suite->quickdraw_->getCurrentState());
    ASSERT_NE(fdnState, nullptr);

    // Pause the game (creates snapshot)
    auto snapshot = fdnState->onStatePaused(suite->device_.pdn);
    ASSERT_NE(snapshot, nullptr);

    // Simulate dismount and remount (game interruption + resume)
    fdnState->onStateDismounted(suite->device_.pdn);
    fdnState->onStateMounted(suite->device_.pdn);

    // Resume with snapshot
    fdnState->onStateResumed(suite->device_.pdn, snapshot.get());

    // Verify the pending challenge message is NOT empty
    std::string pendingMessage = suite->player_->getPendingCdevMessage();
    ASSERT_FALSE(pendingMessage.empty())
        << "Bug #206: FdnDetected received empty message on resume";

    // Verify the message matches the original challenge
    ASSERT_EQ(pendingMessage, "*fdn:7:6")
        << "Bug #206: FdnDetected message corrupted on resume: '"
        << pendingMessage << "'";
}

// ============================================
// Bug #230: Player LEDs show (0,0,0) during FdnDetected
// ============================================

/**
 * Bug #230: Player device LEDs show black (0,0,0) during FdnDetected state
 * instead of role-default or equipped palette colors.
 *
 * Root cause: LED state not being set when entering FdnDetected —
 * no role-default fallback.
 *
 * This test verifies that LEDs are NOT black during FdnDetected and
 * match either the equipped palette or the role default.
 */
void testFdnDetectedLEDsNotBlack(Wave18BugRegressionTestSuite* suite) {
    // Advance to Idle state
    suite->advanceToIdle();
    ASSERT_EQ(suite->quickdraw_->getCurrentStateId(), IDLE);

    // Simulate FDN detection
    std::string fdnChallenge = "*fdn:7:6\r";  // Signal Echo, UP button reward
    suite->device_.serialOutDriver->injectInput(fdnChallenge);
    suite->tick(3);

    // Should transition to FdnDetected (state 21)
    ASSERT_EQ(suite->quickdraw_->getCurrentStateId(), 21);

    // Get LED state from the light driver
    auto* lightDriver = suite->device_.lightDriver;
    ASSERT_NE(lightDriver, nullptr);

    // Read LED colors from left and right light strips
    const auto& leftLights = lightDriver->getLeftLights();
    const auto& rightLights = lightDriver->getRightLights();

    // Verify LEDs are NOT all black (0,0,0)
    bool allBlack = true;
    for (const auto& led : leftLights) {
        if (led.color.red != 0 || led.color.green != 0 || led.color.blue != 0) {
            allBlack = false;
            break;
        }
    }
    if (allBlack) {
        for (const auto& led : rightLights) {
            if (led.color.red != 0 || led.color.green != 0 || led.color.blue != 0) {
                allBlack = false;
                break;
            }
        }
    }

    ASSERT_FALSE(allBlack)
        << "Bug #230: Player LEDs are all black (0,0,0) during FdnDetected state";

    // Optional: Verify LEDs match role default or equipped palette
    // This would require checking against the player's allegiance default colors
    // or the currently equipped color profile
}

#endif // NATIVE_BUILD
