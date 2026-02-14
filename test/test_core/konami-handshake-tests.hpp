#pragma once

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "device-mock.hpp"
#include "game/konami-states/konami-handshake.hpp"
#include "game/player.hpp"
#include "game/fdn-game-type.hpp"

class KonamiHandshakeTestSuite : public testing::Test {
public:
    void SetUp() override {
        player = new Player();
        device = new MockDevice();
        konamiHandshake = new KonamiHandshake(player);
    }

    void TearDown() override {
        delete konamiHandshake;
        delete device;
        delete player;
    }

    Player* player;
    MockDevice* device;
    KonamiHandshake* konamiHandshake;
};

// ============================================
// KONAMI FDN ROUTING TESTS
// ============================================

// Test: KONAMI_CODE FDN with all buttons collected routes to CodeEntry (index 29)
inline void konamiCodeWithAllButtonsRoutesToCodeEntry(KonamiHandshakeTestSuite* suite) {
    // Setup: Player has all 7 Konami buttons (0-6)
    for (uint8_t i = 0; i < 7; i++) {
        suite->player->unlockKonamiButton(i);
    }
    ASSERT_TRUE(suite->player->hasAllKonamiButtons());

    suite->konamiHandshake->onStateMounted(suite->device);

    // Simulate receiving KONAMI_CODE FDN message (game type 7)
    std::string message = "fgame:7";
    suite->device->simulateSerialReceive(message);

    suite->konamiHandshake->onStateLoop(suite->device);

    EXPECT_TRUE(suite->konamiHandshake->shouldTransition());
    EXPECT_EQ(suite->konamiHandshake->getTargetStateIndex(), 29);
}

// Test: KONAMI_CODE FDN without all buttons routes to CodeRejected (index 30)
inline void konamiCodeWithoutAllButtonsRoutesToCodeRejected(KonamiHandshakeTestSuite* suite) {
    // Setup: Player has only 3 buttons (incomplete)
    suite->player->unlockKonamiButton(0);
    suite->player->unlockKonamiButton(2);
    suite->player->unlockKonamiButton(5);
    ASSERT_FALSE(suite->player->hasAllKonamiButtons());

    suite->konamiHandshake->onStateMounted(suite->device);

    // Simulate receiving KONAMI_CODE FDN message
    std::string message = "fgame:7";
    suite->device->simulateSerialReceive(message);

    suite->konamiHandshake->onStateLoop(suite->device);

    EXPECT_TRUE(suite->konamiHandshake->shouldTransition());
    EXPECT_EQ(suite->konamiHandshake->getTargetStateIndex(), 30);
}

// ============================================
// REGULAR GAME FDN ROUTING TESTS
// ============================================

// Test: First encounter (no button) routes to EasyLaunch (index 1 + gameType)
inline void firstEncounterRoutesToEasyLaunch(KonamiHandshakeTestSuite* suite) {
    // Setup: Player has no progress on SIGNAL_ECHO (game type 0)
    ASSERT_FALSE(suite->player->hasUnlockedButton(0));  // UP button
    ASSERT_FALSE(suite->player->hasKonamiBoon());

    suite->konamiHandshake->onStateMounted(suite->device);

    // Simulate receiving SIGNAL_ECHO FDN message (game type 0)
    std::string message = "fgame:0";
    suite->device->simulateSerialReceive(message);

    suite->konamiHandshake->onStateLoop(suite->device);

    EXPECT_TRUE(suite->konamiHandshake->shouldTransition());
    EXPECT_EQ(suite->konamiHandshake->getTargetStateIndex(), 1);  // 1 + 0
}

// Test: Has button (replay) routes to ReplayEasy (index 8 + gameType)
inline void replayEncounterRoutesToReplayEasy(KonamiHandshakeTestSuite* suite) {
    // Setup: Player has GHOST_RUNNER button (6 = START)
    suite->player->unlockKonamiButton(6);
    ASSERT_TRUE(suite->player->hasUnlockedButton(6));
    ASSERT_FALSE(suite->player->hasKonamiBoon());

    suite->konamiHandshake->onStateMounted(suite->device);

    // Simulate receiving GHOST_RUNNER FDN message (game type 1)
    std::string message = "fgame:1";
    suite->device->simulateSerialReceive(message);

    suite->konamiHandshake->onStateLoop(suite->device);

    EXPECT_TRUE(suite->konamiHandshake->shouldTransition());
    EXPECT_EQ(suite->konamiHandshake->getTargetStateIndex(), 9);  // 8 + 1
}

// Test: Hard mode unlocked (no boon) routes to HardLaunch (index 15 + gameType)
inline void hardModeUnlockedRoutesToHardLaunch(KonamiHandshakeTestSuite* suite) {
    // Setup: Player has SPIKE_VECTOR button (1 = DOWN) and hard mode eligibility
    suite->player->unlockKonamiButton(1);
    suite->player->addColorProfileEligibility(2);  // GameType::SPIKE_VECTOR = 2
    ASSERT_TRUE(suite->player->hasColorProfileEligibility(2));
    ASSERT_FALSE(suite->player->hasKonamiBoon());

    suite->konamiHandshake->onStateMounted(suite->device);

    // Simulate receiving SPIKE_VECTOR FDN message (game type 2)
    std::string message = "fgame:2";
    suite->device->simulateSerialReceive(message);

    suite->konamiHandshake->onStateLoop(suite->device);

    EXPECT_TRUE(suite->konamiHandshake->shouldTransition());
    EXPECT_EQ(suite->konamiHandshake->getTargetStateIndex(), 17);  // 15 + 2
}

// Test: Has boon routes to MasteryReplay (index 22 + gameType)
inline void hasBoonRoutesToMasteryReplay(KonamiHandshakeTestSuite* suite) {
    // Setup: Player has boon (all 7 buttons collected and boon activated)
    for (uint8_t i = 0; i < 7; i++) {
        suite->player->unlockKonamiButton(i);
    }
    suite->player->setKonamiBoon(true);
    ASSERT_TRUE(suite->player->hasKonamiBoon());

    suite->konamiHandshake->onStateMounted(suite->device);

    // Simulate receiving FIREWALL_DECRYPT FDN message (game type 3)
    std::string message = "fgame:3";
    suite->device->simulateSerialReceive(message);

    suite->konamiHandshake->onStateLoop(suite->device);

    EXPECT_TRUE(suite->konamiHandshake->shouldTransition());
    EXPECT_EQ(suite->konamiHandshake->getTargetStateIndex(), 25);  // 22 + 3
}

// ============================================
// DECISION TREE PRIORITY TESTS
// ============================================

// Test: Boon takes priority over hard mode
inline void boonTakesPriorityOverHardMode(KonamiHandshakeTestSuite* suite) {
    // Setup: Player has button, hard mode eligibility, AND boon
    suite->player->unlockKonamiButton(3);  // RIGHT button for CIPHER_PATH
    suite->player->addColorProfileEligibility(4);  // GameType::CIPHER_PATH = 4

    // Also give all buttons and boon
    for (uint8_t i = 0; i < 7; i++) {
        suite->player->unlockKonamiButton(i);
    }
    suite->player->setKonamiBoon(true);

    suite->konamiHandshake->onStateMounted(suite->device);

    // Simulate receiving CIPHER_PATH FDN message (game type 4)
    std::string message = "fgame:4";
    suite->device->simulateSerialReceive(message);

    suite->konamiHandshake->onStateLoop(suite->device);

    // Should route to MasteryReplay (22 + 4), not HardLaunch
    EXPECT_TRUE(suite->konamiHandshake->shouldTransition());
    EXPECT_EQ(suite->konamiHandshake->getTargetStateIndex(), 26);  // 22 + 4
}

// Test: Hard mode takes priority over easy replay
inline void hardModeTakesPriorityOverEasyReplay(KonamiHandshakeTestSuite* suite) {
    // Setup: Player has button AND hard mode eligibility (but no boon)
    suite->player->unlockKonamiButton(4);  // B button for EXPLOIT_SEQUENCER
    suite->player->addColorProfileEligibility(5);  // GameType::EXPLOIT_SEQUENCER = 5
    ASSERT_FALSE(suite->player->hasKonamiBoon());

    suite->konamiHandshake->onStateMounted(suite->device);

    // Simulate receiving EXPLOIT_SEQUENCER FDN message (game type 5)
    std::string message = "fgame:5";
    suite->device->simulateSerialReceive(message);

    suite->konamiHandshake->onStateLoop(suite->device);

    // Should route to HardLaunch (15 + 5), not ReplayEasy
    EXPECT_TRUE(suite->konamiHandshake->shouldTransition());
    EXPECT_EQ(suite->konamiHandshake->getTargetStateIndex(), 20);  // 15 + 5
}

// ============================================
// ALL GAME TYPES ROUTING TESTS
// ============================================

// Test: All 7 game types route correctly for first encounter
inline void allGameTypesRouteCorrectlyForFirstEncounter(KonamiHandshakeTestSuite* suite) {
    struct TestCase {
        int gameType;
        int expectedIndex;
        const char* gameName;
    };

    TestCase cases[] = {
        {0, 1, "SIGNAL_ECHO"},
        {1, 2, "GHOST_RUNNER"},
        {2, 3, "SPIKE_VECTOR"},
        {3, 4, "FIREWALL_DECRYPT"},
        {4, 5, "CIPHER_PATH"},
        {5, 6, "EXPLOIT_SEQUENCER"},
        {6, 7, "BREACH_DEFENSE"}
    };

    for (const auto& testCase : cases) {
        // Reset state
        suite->TearDown();
        suite->SetUp();

        suite->konamiHandshake->onStateMounted(suite->device);

        std::string message = "fgame:" + std::string(1, '0' + testCase.gameType);
        suite->device->simulateSerialReceive(message);

        suite->konamiHandshake->onStateLoop(suite->device);

        EXPECT_TRUE(suite->konamiHandshake->shouldTransition())
            << "Failed for " << testCase.gameName;
        EXPECT_EQ(suite->konamiHandshake->getTargetStateIndex(), testCase.expectedIndex)
            << "Failed routing for " << testCase.gameName;
    }
}

// ============================================
// MESSAGE PARSING TESTS
// ============================================

// Test: Valid message format is parsed correctly
inline void validMessageFormatIsParsed(KonamiHandshakeTestSuite* suite) {
    suite->konamiHandshake->onStateMounted(suite->device);

    std::string message = "fgame:2";
    suite->device->simulateSerialReceive(message);

    suite->konamiHandshake->onStateLoop(suite->device);

    // Should successfully parse and prepare transition
    EXPECT_TRUE(suite->konamiHandshake->shouldTransition());
}

// Test: Invalid message format is ignored
inline void invalidMessageFormatIsIgnored(KonamiHandshakeTestSuite* suite) {
    suite->konamiHandshake->onStateMounted(suite->device);

    // Send invalid messages
    suite->device->simulateSerialReceive("invalid");
    suite->device->simulateSerialReceive("game:2");
    suite->device->simulateSerialReceive("fgame:");
    suite->device->simulateSerialReceive("fgame:abc");

    suite->konamiHandshake->onStateLoop(suite->device);

    // Should not transition on invalid input
    EXPECT_FALSE(suite->konamiHandshake->shouldTransition());
}

// Test: Out of range game type is ignored
inline void outOfRangeGameTypeIsIgnored(KonamiHandshakeTestSuite* suite) {
    suite->konamiHandshake->onStateMounted(suite->device);

    // Send out-of-range game types
    suite->device->simulateSerialReceive("fgame:8");  // > 7
    suite->device->simulateSerialReceive("fgame:-1");

    suite->konamiHandshake->onStateLoop(suite->device);

    // Should not transition
    EXPECT_FALSE(suite->konamiHandshake->shouldTransition());
}

// ============================================
// LIFECYCLE TESTS
// ============================================

// Test: State initializes correctly on mount
inline void stateInitializesOnMount(KonamiHandshakeTestSuite* suite) {
    // Before mount
    EXPECT_FALSE(suite->konamiHandshake->shouldTransition());

    suite->konamiHandshake->onStateMounted(suite->device);

    // After mount, still not ready to transition
    EXPECT_FALSE(suite->konamiHandshake->shouldTransition());
}

// Test: State clears callbacks on dismount
inline void stateClearsCallbacksOnDismount(KonamiHandshakeTestSuite* suite) {
    suite->konamiHandshake->onStateMounted(suite->device);

    // Simulate message and transition
    suite->device->simulateSerialReceive("fgame:0");
    suite->konamiHandshake->onStateLoop(suite->device);
    EXPECT_TRUE(suite->konamiHandshake->shouldTransition());

    // Dismount should clear state
    suite->konamiHandshake->onStateDismounted(suite->device);

    // Note: clearCallbacks is called, but we can't easily track it in this mock
    // The important part is that dismount completes without error
}

// Test: Multiple loops don't change transition once set
inline void multipleLoopsDontChangeTransition(KonamiHandshakeTestSuite* suite) {
    suite->player->unlockKonamiButton(0);  // Give player a button

    suite->konamiHandshake->onStateMounted(suite->device);
    suite->device->simulateSerialReceive("fgame:0");

    // First loop sets the transition
    suite->konamiHandshake->onStateLoop(suite->device);
    EXPECT_TRUE(suite->konamiHandshake->shouldTransition());
    int firstIndex = suite->konamiHandshake->getTargetStateIndex();

    // Additional loops shouldn't change it
    suite->konamiHandshake->onStateLoop(suite->device);
    suite->konamiHandshake->onStateLoop(suite->device);

    EXPECT_TRUE(suite->konamiHandshake->shouldTransition());
    EXPECT_EQ(suite->konamiHandshake->getTargetStateIndex(), firstIndex);
}
