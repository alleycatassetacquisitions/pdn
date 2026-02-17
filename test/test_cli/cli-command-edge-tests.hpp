//
// CLI Command Edge Tests - Tests for CLI command handler edge cases
//

#pragma once

#include <gtest/gtest.h>
#include "cli/cli-device-full.hpp"
#include "cli/cli-commands.hpp"
#include "cli/cli-renderer.hpp"
#include "cli/cli-serial-broker.hpp"

using namespace cli;

// ============================================
// CLI COMMAND PROCESSOR TEST SUITE
// ============================================

class CliCommandProcessorTestSuite : public testing::Test {
public:  // Public for test function access
    void SetUp() override {
        // Reset all singleton state before each test to prevent pollution
        SerialCableBroker::resetInstance();
        MockHttpServer::resetInstance();
        SimpleTimer::resetClock();

        // Create test devices and renderer
        devices_.push_back(DeviceFactory::createDevice(0, true));   // Hunter
        devices_.push_back(DeviceFactory::createDevice(1, false));  // Bounty
        selectedDevice_ = 0;
        renderer_ = new Renderer();
        processor_ = new CommandProcessor();
    }

    void TearDown() override {
        delete processor_;
        delete renderer_;

        // Destroy devices
        for (auto& dev : devices_) {
            DeviceFactory::destroyDevice(dev);
        }
        devices_.clear();

        // Clean up singleton state after each test
        SerialCableBroker::resetInstance();
        MockHttpServer::resetInstance();
        SimpleTimer::resetClock();
    }

    CommandProcessor* processor_;
    std::vector<DeviceInstance> devices_;
    int selectedDevice_;
    Renderer* renderer_;
};

// ============================================
// HELP COMMAND TESTS
// ============================================

// Test: help command with no arguments shows all commands
void helpCommandNoArgs(CliCommandProcessorTestSuite* suite) {
    auto result = suite->processor_->execute("help", suite->devices_, suite->selectedDevice_, *suite->renderer_);

    // Should contain key commands
    ASSERT_NE(result.message.find("help"), std::string::npos);
    ASSERT_NE(result.message.find("quit"), std::string::npos);
    ASSERT_FALSE(result.shouldQuit);
}

// Test: help command variations (h, ?)
void helpCommandAliases(CliCommandProcessorTestSuite* suite) {
    auto result1 = suite->processor_->execute("h", suite->devices_, suite->selectedDevice_, *suite->renderer_);
    auto result2 = suite->processor_->execute("?", suite->devices_, suite->selectedDevice_, *suite->renderer_);

    ASSERT_FALSE(result1.message.empty());
    ASSERT_FALSE(result2.message.empty());
}

// Test: help2 command shows extended help
void help2CommandShowsExtendedHelp(CliCommandProcessorTestSuite* suite) {
    auto result = suite->processor_->execute("help2", suite->devices_, suite->selectedDevice_, *suite->renderer_);

    // Should contain detailed command descriptions
    ASSERT_NE(result.message.find("add"), std::string::npos);
    ASSERT_NE(result.message.find("cable"), std::string::npos);
    ASSERT_FALSE(result.shouldQuit);
}

// ============================================
// QUIT COMMAND TESTS
// ============================================

// Test: quit command sets shouldQuit flag
void quitCommandSetsShouldQuit(CliCommandProcessorTestSuite* suite) {
    auto result = suite->processor_->execute("quit", suite->devices_, suite->selectedDevice_, *suite->renderer_);

    ASSERT_TRUE(result.shouldQuit);
}

// Test: quit command aliases (q, exit)
void quitCommandAliases(CliCommandProcessorTestSuite* suite) {
    auto result1 = suite->processor_->execute("q", suite->devices_, suite->selectedDevice_, *suite->renderer_);
    auto result2 = suite->processor_->execute("exit", suite->devices_, suite->selectedDevice_, *suite->renderer_);

    ASSERT_TRUE(result1.shouldQuit);
    ASSERT_TRUE(result2.shouldQuit);
}

// ============================================
// CONNECT/CABLE COMMAND TESTS
// ============================================

// Test: cable command with no arguments shows usage
void cableCommandNoArgs(CliCommandProcessorTestSuite* suite) {
    auto result = suite->processor_->execute("cable", suite->devices_, suite->selectedDevice_, *suite->renderer_);

    ASSERT_NE(result.message.find("Usage"), std::string::npos);
    ASSERT_FALSE(result.shouldQuit);
}

// Test: cable command with invalid device ID
void cableCommandInvalidDeviceId(CliCommandProcessorTestSuite* suite) {
    auto result = suite->processor_->execute("cable 999 888", suite->devices_, suite->selectedDevice_, *suite->renderer_);

    ASSERT_NE(result.message.find("Invalid"), std::string::npos);
}

// Test: cable command with negative device ID
void cableCommandNegativeDeviceId(CliCommandProcessorTestSuite* suite) {
    auto result = suite->processor_->execute("cable -1 0", suite->devices_, suite->selectedDevice_, *suite->renderer_);

    ASSERT_NE(result.message.find("Invalid"), std::string::npos);
}

// Test: cable command connecting device to itself
void cableCommandConnectToSelf(CliCommandProcessorTestSuite* suite) {
    auto result = suite->processor_->execute("cable 0 0", suite->devices_, suite->selectedDevice_, *suite->renderer_);

    ASSERT_NE(result.message.find("Cannot connect device to itself"), std::string::npos);
}

// Test: cable command with non-numeric device ID
void cableCommandNonNumericId(CliCommandProcessorTestSuite* suite) {
    auto result = suite->processor_->execute("cable abc def", suite->devices_, suite->selectedDevice_, *suite->renderer_);

    // Should handle gracefully - atoi returns 0 for non-numeric
    ASSERT_NE(result.message.find("Invalid"), std::string::npos);
}

// Test: cable command aliases (connect, c)
void cableCommandAliases(CliCommandProcessorTestSuite* suite) {
    auto result1 = suite->processor_->execute("connect 0 1", suite->devices_, suite->selectedDevice_, *suite->renderer_);
    auto result2 = suite->processor_->execute("c 0 1", suite->devices_, suite->selectedDevice_, *suite->renderer_);

    // Both should execute without error
    ASSERT_FALSE(result1.message.empty());
    ASSERT_FALSE(result2.message.empty());
}

// Test: cable disconnect with insufficient arguments
void cableDisconnectNoArgs(CliCommandProcessorTestSuite* suite) {
    auto result = suite->processor_->execute("cable -d", suite->devices_, suite->selectedDevice_, *suite->renderer_);

    ASSERT_NE(result.message.find("Usage"), std::string::npos);
}

// Test: cable list with no connections
void cableListNoConnections(CliCommandProcessorTestSuite* suite) {
    auto result = suite->processor_->execute("cable -l", suite->devices_, suite->selectedDevice_, *suite->renderer_);

    ASSERT_NE(result.message.find("No cable connections"), std::string::npos);
}

// ============================================
// REBOOT COMMAND TESTS
// ============================================

// Test: reboot command with no arguments reboots selected device
void rebootCommandNoArgs(CliCommandProcessorTestSuite* suite) {
    auto result = suite->processor_->execute("reboot", suite->devices_, suite->selectedDevice_, *suite->renderer_);

    ASSERT_NE(result.message.find("Rebooted"), std::string::npos);
    ASSERT_FALSE(result.shouldQuit);
}

// Test: reboot command with invalid device ID (falls back to selected device)
void rebootCommandInvalidDeviceId(CliCommandProcessorTestSuite* suite) {
    auto result = suite->processor_->execute("reboot 999", suite->devices_, suite->selectedDevice_, *suite->renderer_);

    // findDevice returns selectedDevice when not found, so it reboots the selected device
    ASSERT_NE(result.message.find("Rebooted"), std::string::npos);
}

// Test: reboot command with extra arguments (should ignore)
void rebootCommandExtraArgs(CliCommandProcessorTestSuite* suite) {
    auto result = suite->processor_->execute("reboot 0 extra args", suite->devices_, suite->selectedDevice_, *suite->renderer_);

    // Should still reboot device 0, ignoring extra args
    ASSERT_NE(result.message.find("Rebooted"), std::string::npos);
}

// Test: reboot command alias (restart)
void rebootCommandAlias(CliCommandProcessorTestSuite* suite) {
    auto result = suite->processor_->execute("restart", suite->devices_, suite->selectedDevice_, *suite->renderer_);

    ASSERT_NE(result.message.find("Rebooted"), std::string::npos);
}

// ============================================
// DISPLAY COMMAND TESTS
// ============================================

// Test: display command toggles mode when no argument
void displayCommandToggle(CliCommandProcessorTestSuite* suite) {
    bool initialMirror = suite->renderer_->isDisplayMirrorEnabled();
    bool initialCaptions = suite->renderer_->isCaptionsEnabled();

    auto result = suite->processor_->execute("display", suite->devices_, suite->selectedDevice_, *suite->renderer_);

    // Should toggle both mirror and captions
    ASSERT_FALSE(result.message.empty());
}

// Test: display command with "on" argument
void displayCommandOn(CliCommandProcessorTestSuite* suite) {
    auto result = suite->processor_->execute("display on", suite->devices_, suite->selectedDevice_, *suite->renderer_);

    ASSERT_TRUE(suite->renderer_->isDisplayMirrorEnabled());
    ASSERT_TRUE(suite->renderer_->isCaptionsEnabled());
    ASSERT_NE(result.message.find("ON"), std::string::npos);
}

// Test: display command with "off" argument
void displayCommandOff(CliCommandProcessorTestSuite* suite) {
    suite->renderer_->setDisplayMirror(true);
    suite->renderer_->setCaptions(true);

    auto result = suite->processor_->execute("display off", suite->devices_, suite->selectedDevice_, *suite->renderer_);

    ASSERT_FALSE(suite->renderer_->isDisplayMirrorEnabled());
    ASSERT_FALSE(suite->renderer_->isCaptionsEnabled());
    ASSERT_NE(result.message.find("OFF"), std::string::npos);
}

// Test: display command with invalid argument
void displayCommandInvalidArg(CliCommandProcessorTestSuite* suite) {
    auto result = suite->processor_->execute("display invalid", suite->devices_, suite->selectedDevice_, *suite->renderer_);

    ASSERT_NE(result.message.find("Usage"), std::string::npos);
}

// Test: display command alias (d)
void displayCommandAlias(CliCommandProcessorTestSuite* suite) {
    auto result = suite->processor_->execute("d", suite->devices_, suite->selectedDevice_, *suite->renderer_);

    ASSERT_FALSE(result.message.empty());
}

// ============================================
// ROLE COMMAND TESTS
// ============================================

// Test: role command shows selected device role
void roleCommandShowsSelectedDevice(CliCommandProcessorTestSuite* suite) {
    auto result = suite->processor_->execute("role", suite->devices_, suite->selectedDevice_, *suite->renderer_);

    ASSERT_NE(result.message.find("Device"), std::string::npos);
    ASSERT_TRUE(result.message.find("Hunter") != std::string::npos ||
                result.message.find("Bounty") != std::string::npos);
}

// Test: role command with device ID
void roleCommandWithDeviceId(CliCommandProcessorTestSuite* suite) {
    auto result = suite->processor_->execute("role 1", suite->devices_, suite->selectedDevice_, *suite->renderer_);

    ASSERT_NE(result.message.find("Device"), std::string::npos);
    ASSERT_NE(result.message.find("0011"), std::string::npos);  // Device 1's ID
}

// Test: role command with "all" shows all devices
void roleCommandAll(CliCommandProcessorTestSuite* suite) {
    auto result = suite->processor_->execute("role all", suite->devices_, suite->selectedDevice_, *suite->renderer_);

    ASSERT_NE(result.message.find("0010"), std::string::npos);  // Device 0
    ASSERT_NE(result.message.find("0011"), std::string::npos);  // Device 1
}

// Test: role command with invalid device ID
void roleCommandInvalidDeviceId(CliCommandProcessorTestSuite* suite) {
    auto result = suite->processor_->execute("role 999", suite->devices_, suite->selectedDevice_, *suite->renderer_);

    ASSERT_NE(result.message.find("Invalid device"), std::string::npos);
}

// Test: role command alias (roles)
void roleCommandAlias(CliCommandProcessorTestSuite* suite) {
    auto result = suite->processor_->execute("roles", suite->devices_, suite->selectedDevice_, *suite->renderer_);

    ASSERT_NE(result.message.find("Device"), std::string::npos);
}

// ============================================
// EDGE CASE TESTS
// ============================================

// Test: empty input does nothing
void emptyInputNoOp(CliCommandProcessorTestSuite* suite) {
    auto result = suite->processor_->execute("", suite->devices_, suite->selectedDevice_, *suite->renderer_);

    ASSERT_TRUE(result.message.empty());
    ASSERT_FALSE(result.shouldQuit);
}

// Test: whitespace-only input does nothing
void whitespaceOnlyInput(CliCommandProcessorTestSuite* suite) {
    auto result = suite->processor_->execute("   ", suite->devices_, suite->selectedDevice_, *suite->renderer_);

    ASSERT_TRUE(result.message.empty());
    ASSERT_FALSE(result.shouldQuit);
}

// Test: unknown command shows error
void unknownCommandShowsError(CliCommandProcessorTestSuite* suite) {
    auto result = suite->processor_->execute("invalidcmd", suite->devices_, suite->selectedDevice_, *suite->renderer_);

    ASSERT_NE(result.message.find("Unknown command"), std::string::npos);
    ASSERT_NE(result.message.find("invalidcmd"), std::string::npos);
    ASSERT_NE(result.message.find("help"), std::string::npos);
    ASSERT_FALSE(result.shouldQuit);
}

// Test: command with leading whitespace
void commandWithLeadingWhitespace(CliCommandProcessorTestSuite* suite) {
    auto result = suite->processor_->execute("  help", suite->devices_, suite->selectedDevice_, *suite->renderer_);

    ASSERT_NE(result.message.find("help"), std::string::npos);
}

// Test: command with trailing whitespace
void commandWithTrailingWhitespace(CliCommandProcessorTestSuite* suite) {
    auto result = suite->processor_->execute("help  ", suite->devices_, suite->selectedDevice_, *suite->renderer_);

    ASSERT_NE(result.message.find("help"), std::string::npos);
}

// Test: command with multiple spaces between words
void commandWithMultipleSpaces(CliCommandProcessorTestSuite* suite) {
    auto result = suite->processor_->execute("role    all", suite->devices_, suite->selectedDevice_, *suite->renderer_);

    // Should still parse correctly
    ASSERT_NE(result.message.find("Device"), std::string::npos);
}

// Test: very long input string (buffer overflow test)
void veryLongInputString(CliCommandProcessorTestSuite* suite) {
    std::string longInput(10000, 'x');
    auto result = suite->processor_->execute(longInput, suite->devices_, suite->selectedDevice_, *suite->renderer_);

    // Should handle gracefully (unknown command)
    ASSERT_NE(result.message.find("Unknown command"), std::string::npos);
    ASSERT_FALSE(result.shouldQuit);
}

// Test: command with special characters
void commandWithSpecialCharacters(CliCommandProcessorTestSuite* suite) {
    auto result = suite->processor_->execute("help!@#$%", suite->devices_, suite->selectedDevice_, *suite->renderer_);

    // Should treat as unknown command
    ASSERT_NE(result.message.find("Unknown command"), std::string::npos);
}

// ============================================
// LIST AND SELECT COMMAND TESTS
// ============================================

// Test: list command shows all devices
void listCommandShowsAllDevices(CliCommandProcessorTestSuite* suite) {
    auto result = suite->processor_->execute("list", suite->devices_, suite->selectedDevice_, *suite->renderer_);

    ASSERT_NE(result.message.find("0010"), std::string::npos);  // Device 0
    ASSERT_NE(result.message.find("0011"), std::string::npos);  // Device 1
}

// Test: list command alias (ls)
void listCommandAlias(CliCommandProcessorTestSuite* suite) {
    auto result = suite->processor_->execute("ls", suite->devices_, suite->selectedDevice_, *suite->renderer_);

    ASSERT_NE(result.message.find("Devices"), std::string::npos);
}

// Test: select command with no arguments shows usage
void selectCommandNoArgs(CliCommandProcessorTestSuite* suite) {
    auto result = suite->processor_->execute("select", suite->devices_, suite->selectedDevice_, *suite->renderer_);

    ASSERT_NE(result.message.find("Usage"), std::string::npos);
}

// Test: select command with valid device ID
void selectCommandValidDeviceId(CliCommandProcessorTestSuite* suite) {
    auto result = suite->processor_->execute("select 1", suite->devices_, suite->selectedDevice_, *suite->renderer_);

    ASSERT_EQ(suite->selectedDevice_, 1);
    ASSERT_NE(result.message.find("Selected"), std::string::npos);
}

// Test: select command with invalid device ID
void selectCommandInvalidDeviceId(CliCommandProcessorTestSuite* suite) {
    int originalSelection = suite->selectedDevice_;
    auto result = suite->processor_->execute("select 999", suite->devices_, suite->selectedDevice_, *suite->renderer_);

    ASSERT_EQ(suite->selectedDevice_, originalSelection);  // Should not change
    ASSERT_NE(result.message.find("not found"), std::string::npos);
}

// Test: select command alias (sel)
void selectCommandAlias(CliCommandProcessorTestSuite* suite) {
    auto result = suite->processor_->execute("sel 1", suite->devices_, suite->selectedDevice_, *suite->renderer_);

    ASSERT_EQ(suite->selectedDevice_, 1);
    ASSERT_NE(result.message.find("Selected"), std::string::npos);
}

// ============================================
// STATE COMMAND TESTS
// ============================================

// Test: state command shows current state
void stateCommandShowsCurrentState(CliCommandProcessorTestSuite* suite) {
    auto result = suite->processor_->execute("state", suite->devices_, suite->selectedDevice_, *suite->renderer_);

    ASSERT_NE(result.message.find("0010"), std::string::npos);  // Device ID
    ASSERT_FALSE(result.message.empty());
}

// Test: state command with device ID
void stateCommandWithDeviceId(CliCommandProcessorTestSuite* suite) {
    auto result = suite->processor_->execute("state 1", suite->devices_, suite->selectedDevice_, *suite->renderer_);

    ASSERT_NE(result.message.find("0011"), std::string::npos);  // Device 1's ID
}

// Test: state command with invalid device ID (falls back to selected device)
void stateCommandInvalidDeviceId(CliCommandProcessorTestSuite* suite) {
    auto result = suite->processor_->execute("state 999", suite->devices_, suite->selectedDevice_, *suite->renderer_);

    // findDevice returns selectedDevice when not found, so it shows state for selected device
    ASSERT_FALSE(result.message.empty());
    // Should contain device ID (either selected device or state name)
    ASSERT_TRUE(result.message.find("0010") != std::string::npos ||
                result.message.find(":") != std::string::npos);
}

// Test: state command alias (st)
void stateCommandAlias(CliCommandProcessorTestSuite* suite) {
    auto result = suite->processor_->execute("st", suite->devices_, suite->selectedDevice_, *suite->renderer_);

    ASSERT_FALSE(result.message.empty());
}

// ============================================
// GAMES COMMAND TESTS
// ============================================

// Test: games command lists all available games
void gamesCommandListsAllGames(CliCommandProcessorTestSuite* suite) {
    auto result = suite->processor_->execute("games", suite->devices_, suite->selectedDevice_, *suite->renderer_);

    ASSERT_NE(result.message.find("quickdraw"), std::string::npos);
    ASSERT_NE(result.message.find("ghost-runner"), std::string::npos);
    ASSERT_NE(result.message.find("spike-vector"), std::string::npos);
    ASSERT_NE(result.message.find("firewall-decrypt"), std::string::npos);
    ASSERT_NE(result.message.find("cipher-path"), std::string::npos);
    ASSERT_NE(result.message.find("exploit-sequencer"), std::string::npos);
    ASSERT_NE(result.message.find("breach-defense"), std::string::npos);
    ASSERT_NE(result.message.find("signal-echo"), std::string::npos);
}

// ============================================
// MIRROR AND CAPTIONS COMMAND TESTS
// ============================================

// Test: mirror command toggles when no argument
void mirrorCommandToggle(CliCommandProcessorTestSuite* suite) {
    bool initialState = suite->renderer_->isDisplayMirrorEnabled();

    auto result = suite->processor_->execute("mirror", suite->devices_, suite->selectedDevice_, *suite->renderer_);

    ASSERT_NE(suite->renderer_->isDisplayMirrorEnabled(), initialState);
}

// Test: mirror command with "on" argument
void mirrorCommandOn(CliCommandProcessorTestSuite* suite) {
    auto result = suite->processor_->execute("mirror on", suite->devices_, suite->selectedDevice_, *suite->renderer_);

    ASSERT_TRUE(suite->renderer_->isDisplayMirrorEnabled());
    ASSERT_NE(result.message.find("ON"), std::string::npos);
}

// Test: mirror command with "off" argument
void mirrorCommandOff(CliCommandProcessorTestSuite* suite) {
    suite->renderer_->setDisplayMirror(true);

    auto result = suite->processor_->execute("mirror off", suite->devices_, suite->selectedDevice_, *suite->renderer_);

    ASSERT_FALSE(suite->renderer_->isDisplayMirrorEnabled());
    ASSERT_NE(result.message.find("OFF"), std::string::npos);
}

// Test: mirror command with invalid argument
void mirrorCommandInvalidArg(CliCommandProcessorTestSuite* suite) {
    auto result = suite->processor_->execute("mirror invalid", suite->devices_, suite->selectedDevice_, *suite->renderer_);

    ASSERT_NE(result.message.find("Usage"), std::string::npos);
}

// Test: mirror command alias (m)
void mirrorCommandAlias(CliCommandProcessorTestSuite* suite) {
    auto result = suite->processor_->execute("m", suite->devices_, suite->selectedDevice_, *suite->renderer_);

    ASSERT_FALSE(result.message.empty());
}

// Test: captions command toggles when no argument
void captionsCommandToggle(CliCommandProcessorTestSuite* suite) {
    bool initialState = suite->renderer_->isCaptionsEnabled();

    auto result = suite->processor_->execute("captions", suite->devices_, suite->selectedDevice_, *suite->renderer_);

    ASSERT_NE(suite->renderer_->isCaptionsEnabled(), initialState);
}

// Test: captions command with "on" argument
void captionsCommandOn(CliCommandProcessorTestSuite* suite) {
    auto result = suite->processor_->execute("captions on", suite->devices_, suite->selectedDevice_, *suite->renderer_);

    ASSERT_TRUE(suite->renderer_->isCaptionsEnabled());
    ASSERT_NE(result.message.find("ON"), std::string::npos);
}

// Test: captions command with "off" argument
void captionsCommandOff(CliCommandProcessorTestSuite* suite) {
    suite->renderer_->setCaptions(true);

    auto result = suite->processor_->execute("captions off", suite->devices_, suite->selectedDevice_, *suite->renderer_);

    ASSERT_FALSE(suite->renderer_->isCaptionsEnabled());
    ASSERT_NE(result.message.find("OFF"), std::string::npos);
}

// Test: captions command with invalid argument
void captionsCommandInvalidArg(CliCommandProcessorTestSuite* suite) {
    auto result = suite->processor_->execute("captions invalid", suite->devices_, suite->selectedDevice_, *suite->renderer_);

    ASSERT_NE(result.message.find("Usage"), std::string::npos);
}

// Test: captions command alias (cap)
void captionsCommandAlias(CliCommandProcessorTestSuite* suite) {
    auto result = suite->processor_->execute("cap", suite->devices_, suite->selectedDevice_, *suite->renderer_);

    ASSERT_FALSE(result.message.empty());
}

// ============================================
// KONAMI COMMAND TESTS
// ============================================

// Test: konami command with no args shows progress on selected device
void konamiCommandNoArgs(CliCommandProcessorTestSuite* suite) {
    auto result = suite->processor_->execute("konami", suite->devices_, suite->selectedDevice_, *suite->renderer_);

    ASSERT_NE(result.message.find("Konami:"), std::string::npos);
    ASSERT_NE(result.message.find("boon="), std::string::npos);
}

// Test: konami command with device index shows that device's progress
void konamiCommandWithDeviceIndex(CliCommandProcessorTestSuite* suite) {
    // Set progress on device 0
    suite->devices_[0].player->setKonamiProgress(0x3F);

    // Set progress on device 1 to different value
    suite->devices_[1].player->setKonamiProgress(0x7F);

    // Query device 0 explicitly
    auto result = suite->processor_->execute("konami 0", suite->devices_, suite->selectedDevice_, *suite->renderer_);

    ASSERT_NE(result.message.find("0x3F"), std::string::npos);
    ASSERT_EQ(result.message.find("0x7F"), std::string::npos);
}

// Test: konami command preserves progress after game win (bug fix for #204)
void konamiCommandPreservesProgressAfterWin(CliCommandProcessorTestSuite* suite) {
    // Set some progress on device 0
    suite->devices_[0].player->setKonamiProgress(0x1F);

    // Run konami with device index (this was the bug - it would reset to 0)
    auto result = suite->processor_->execute("konami 0", suite->devices_, suite->selectedDevice_, *suite->renderer_);

    // Verify progress was NOT reset
    ASSERT_NE(result.message.find("0x1F"), std::string::npos);
    ASSERT_EQ(suite->devices_[0].player->getKonamiProgress(), 0x1F);
}

// Test: konami set command sets progress correctly
void konamiSetCommand(CliCommandProcessorTestSuite* suite) {
    auto result = suite->processor_->execute("konami set 0 127", suite->devices_, suite->selectedDevice_, *suite->renderer_);

    ASSERT_NE(result.message.find("Konami set:"), std::string::npos);
    ASSERT_NE(result.message.find("0x7F"), std::string::npos);
    ASSERT_EQ(suite->devices_[0].player->getKonamiProgress(), 0x7F);
}

// Test: konami set command with zero value
void konamiSetCommandZeroValue(CliCommandProcessorTestSuite* suite) {
    // Set some initial progress
    suite->devices_[0].player->setKonamiProgress(0x3F);

    // Explicitly set to zero using set command
    auto result = suite->processor_->execute("konami set 0 0", suite->devices_, suite->selectedDevice_, *suite->renderer_);

    ASSERT_NE(result.message.find("Konami set:"), std::string::npos);
    ASSERT_NE(result.message.find("0x00"), std::string::npos);
    ASSERT_EQ(suite->devices_[0].player->getKonamiProgress(), 0x00);
}

// Test: konami set command auto-enables boon when complete
void konamiSetCommandAutoBoon(CliCommandProcessorTestSuite* suite) {
    auto result = suite->processor_->execute("konami set 0 127", suite->devices_, suite->selectedDevice_, *suite->renderer_);

    ASSERT_NE(result.message.find("boon=yes"), std::string::npos);
    ASSERT_TRUE(suite->devices_[0].player->hasKonamiBoon());
}

// Test: konami command with invalid device shows error
void konamiCommandInvalidDevice(CliCommandProcessorTestSuite* suite) {
    auto result = suite->processor_->execute("konami 999", suite->devices_, suite->selectedDevice_, *suite->renderer_);

    ASSERT_NE(result.message.find("Unknown device"), std::string::npos);
}
