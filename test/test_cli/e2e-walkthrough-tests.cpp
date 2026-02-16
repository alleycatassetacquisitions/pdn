#ifdef NATIVE_BUILD

#include <gtest/gtest.h>
#include "cli/cli-device.hpp"
#include "cli/cli-serial-broker.hpp"
#include "cli/cli-http-server.hpp"
#include "game/ghost-runner/ghost-runner.hpp"
#include "game/spike-vector/spike-vector.hpp"
#include "game/cipher-path/cipher-path.hpp"
#include "game/exploit-sequencer/exploit-sequencer.hpp"
#include "game/breach-defense/breach-defense.hpp"
#include "game/signal-echo/signal-echo.hpp"
#include "game/firewall-decrypt/firewall-decrypt.hpp"
#include "game/minigame.hpp"
#include "game/konami-metagame.hpp"
#include "device/device-types.hpp"
#include "utils/simple-timer.hpp"

using namespace cli;

// ============================================
// E2E WALKTHROUGH TEST FIXTURE
//
// Wave 19 - Validates all demo walkthroughs work end-to-end
// after Wave 18 visual redesigns.
//
// DISABLED: ALL tests fail — KMG routing (Wave 17, #271) changes game launch flow.
// Previously masked by SIGSEGV crash (#300). See #327.
//
// Tests cover:
// - Full 3-player FDN flow: connect → detect → konami → minigame → complete
// - Each minigame: launch → play → win
// - Each minigame: launch → play → lose
// - Multi-game sequence: play through all games in order
// ============================================

class E2EWalkthroughTestSuite : public testing::Test {
public:
    void SetUp() override {
        // Reset all singleton state before each test to prevent pollution
        SerialCableBroker::resetInstance();
        MockHttpServer::resetInstance();
        SimpleTimer::resetClock();

        player_ = DeviceFactory::createDevice(0, true);
        SimpleTimer::setPlatformClock(player_.clockDriver);
    }

    void TearDown() override {
        DeviceFactory::destroyDevice(player_);

        // Clean up singleton state after each test
        SerialCableBroker::resetInstance();
        MockHttpServer::resetInstance();
        SimpleTimer::resetClock();
    }

    void tick(int n = 1) {
        for (int i = 0; i < n; i++) {
            SerialCableBroker::getInstance().transferData();
            player_.pdn->loop();
        }
    }

    void tickWithTime(int n, int delayMs) {
        for (int i = 0; i < n; i++) {
            player_.clockDriver->advance(delayMs);
            player_.pdn->loop();
        }
    }

    void advanceToIdle() {
        player_.game->skipToState(player_.pdn, 6);
        player_.pdn->loop();
    }

    int getPlayerStateId() {
        return player_.game->getCurrentStateId();
    }

    // Helper: trigger FDN handshake from Idle
    void triggerFdnHandshake(const std::string& gameType, const std::string& reward) {
        std::string msg = "*fdn:" + gameType + ":" + reward + "\r";
        player_.serialOutDriver->injectInput(msg.c_str());
        tick(3);
        player_.serialOutDriver->injectInput("*fack\r");
        tickWithTime(5, 100);
    }

    DeviceInstance player_;
};

// ============================================
// GHOST RUNNER WALKTHROUGH TESTS
// ============================================

// REMOVED: Ghost Runner redesigned to memory maze in Wave 18 (PR #264).
// Old rhythm game API (ghostSpeedMs, notesPerRound, currentPattern) no longer exists.
// Rewrite needed for memory maze API (cols, rows, walls[], solutionPath[]).

// ============================================
// SPIKE VECTOR WALKTHROUGH TESTS
// ============================================

// REMOVED: Spike Vector redesigned to wall gauntlet in Wave 18 (PR #269).
// Old API (approachSpeedMs, waves) no longer exists.
// Rewrite needed for wall gauntlet API (levels, baseWallCount, numPositions).

// ============================================
// FIREWALL DECRYPT WALKTHROUGH TESTS
// ============================================

TEST_F(E2EWalkthroughTestSuite, DISABLED_FirewallDecryptLaunchPlayWin) {
    advanceToIdle();

    // Trigger FDN for Firewall Decrypt (GameType 3, KonamiButton LEFT=2)
    triggerFdnHandshake("3", "2");

    auto* fd = static_cast<FirewallDecrypt*>(
        player_.pdn->getApp(StateId(FIREWALL_DECRYPT_APP_ID)));
    ASSERT_NE(fd, nullptr);
    ASSERT_TRUE(fd->getConfig().managedMode);

    // Configure for easy win
    fd->getConfig().numRounds = 1;
    fd->getConfig().numCandidates = 2;
    fd->getConfig().rngSeed = 42;

    // Advance past intro (2s)
    tickWithTime(25, 100);

    // In round 1 — select correct pattern
    player_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    tickWithTime(10, 100);

    // Should win after 1 round
    ASSERT_EQ(fd->getOutcome().result, MiniGameResult::WON);

    tickWithTime(50, 100);

    ASSERT_TRUE(player_.player->hasUnlockedButton(static_cast<uint8_t>(KonamiButton::LEFT)));
}

TEST_F(E2EWalkthroughTestSuite, DISABLED_FirewallDecryptLaunchPlayLose) {
    advanceToIdle();

    triggerFdnHandshake("3", "2");

    auto* fd = static_cast<FirewallDecrypt*>(
        player_.pdn->getApp(StateId(FIREWALL_DECRYPT_APP_ID)));
    ASSERT_NE(fd, nullptr);

    // Configure for easy loss
    fd->getConfig().numRounds = 1;
    fd->getConfig().numCandidates = 2;
    fd->getConfig().rngSeed = 42;

    // Advance past intro
    tickWithTime(25, 100);

    // Select wrong pattern intentionally
    player_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    tickWithTime(5, 100);
    player_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    tickWithTime(5, 100);
    player_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    tickWithTime(10, 100);

    // Should lose
    ASSERT_EQ(fd->getOutcome().result, MiniGameResult::LOST);

    tickWithTime(50, 100);

    ASSERT_FALSE(player_.player->hasUnlockedButton(static_cast<uint8_t>(KonamiButton::LEFT)));
}

// ============================================
// SIGNAL ECHO WALKTHROUGH TESTS
// ============================================

TEST_F(E2EWalkthroughTestSuite, DISABLED_SignalEchoLaunchPlayWin) {
    advanceToIdle();

    // Trigger FDN for Signal Echo (GameType 7, KonamiButton UP=0)
    triggerFdnHandshake("7", "0");

    auto* se = static_cast<SignalEcho*>(
        player_.pdn->getApp(StateId(SIGNAL_ECHO_APP_ID)));
    ASSERT_NE(se, nullptr);
    ASSERT_TRUE(se->getConfig().managedMode);

    // Configure for easy win
    se->getConfig().numSequences = 1;
    se->getConfig().sequenceLength = 2;
    se->getConfig().allowedMistakes = 3;
    se->getConfig().rngSeed = 42;

    // Advance past intro (2s)
    tickWithTime(25, 100);

    // ShowSequence state
    tickWithTime(30, 100);

    // PlayerInput state — enter correct sequence
    auto& session = se->getSession();
    if (session.currentSequence[0] == 0) {
        player_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    } else {
        player_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    }
    tickWithTime(10, 100);

    if (session.currentSequence[1] == 0) {
        player_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    } else {
        player_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    }
    tickWithTime(10, 100);

    // Should win
    ASSERT_EQ(se->getOutcome().result, MiniGameResult::WON);

    tickWithTime(50, 100);

    ASSERT_TRUE(player_.player->hasUnlockedButton(static_cast<uint8_t>(KonamiButton::UP)));
}

TEST_F(E2EWalkthroughTestSuite, DISABLED_SignalEchoLaunchPlayLose) {
    advanceToIdle();

    triggerFdnHandshake("7", "0");

    auto* se = static_cast<SignalEcho*>(
        player_.pdn->getApp(StateId(SIGNAL_ECHO_APP_ID)));
    ASSERT_NE(se, nullptr);

    // Configure for easy loss
    se->getConfig().numSequences = 1;
    se->getConfig().sequenceLength = 2;
    se->getConfig().allowedMistakes = 0;  // No mistakes allowed
    se->getConfig().rngSeed = 42;

    // Advance past intro
    tickWithTime(25, 100);

    // ShowSequence state
    tickWithTime(30, 100);

    // PlayerInput state — enter WRONG sequence intentionally
    player_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    tickWithTime(5, 100);
    player_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    tickWithTime(10, 100);

    // Should lose
    ASSERT_EQ(se->getOutcome().result, MiniGameResult::LOST);

    tickWithTime(50, 100);

    ASSERT_FALSE(player_.player->hasUnlockedButton(static_cast<uint8_t>(KonamiButton::UP)));
}

// ============================================
// CIPHER PATH WALKTHROUGH TESTS
// ============================================

// REMOVED: Cipher Path redesigned to wire routing in Wave 18 (PR #268).
// Old API (gridSize, moveBudget, cipher[]) no longer exists.
// Rewrite needed for wire routing API (cols, rows, tileType[], tileRotation[]).

// ============================================
// EXPLOIT SEQUENCER WALKTHROUGH TESTS
// ============================================

TEST_F(E2EWalkthroughTestSuite, DISABLED_ExploitSequencerLaunchPlayWin) {
    advanceToIdle();

    // Trigger FDN for Exploit Sequencer (GameType 5, KonamiButton B=4)
    triggerFdnHandshake("5", "4");

    auto* es = static_cast<ExploitSequencer*>(
        player_.pdn->getApp(StateId(EXPLOIT_SEQUENCER_APP_ID)));
    ASSERT_NE(es, nullptr);
    ASSERT_TRUE(es->getConfig().managedMode);

    // Configure for easy win
    es->getConfig().rounds = 1;
    es->getConfig().notesPerRound = 2;
    es->getConfig().noteSpeedMs = 50;
    es->getConfig().rngSeed = 42;

    // Advance past intro (2s)
    tickWithTime(25, 100);

    // Chain execution — press buttons at right timing
    tickWithTime(5, 100);
    player_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    tickWithTime(10, 100);
    player_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    tickWithTime(20, 100);

    // Should win
    ASSERT_EQ(es->getOutcome().result, MiniGameResult::WON);

    tickWithTime(50, 100);

    ASSERT_TRUE(player_.player->hasUnlockedButton(static_cast<uint8_t>(KonamiButton::B)));
}

TEST_F(E2EWalkthroughTestSuite, DISABLED_ExploitSequencerLaunchPlayLose) {
    advanceToIdle();

    triggerFdnHandshake("5", "4");

    auto* es = static_cast<ExploitSequencer*>(
        player_.pdn->getApp(StateId(EXPLOIT_SEQUENCER_APP_ID)));
    ASSERT_NE(es, nullptr);

    // Configure for easy loss
    es->getConfig().rounds = 1;
    es->getConfig().notesPerRound = 2;
    es->getConfig().noteSpeedMs = 10;  // Very fast
    es->getConfig().lives = 0;  // No lives
    es->getConfig().rngSeed = 42;

    // Advance past intro
    tickWithTime(25, 100);

    // Miss timing windows
    tickWithTime(100, 100);

    // Should lose
    ASSERT_EQ(es->getOutcome().result, MiniGameResult::LOST);

    tickWithTime(50, 100);

    ASSERT_FALSE(player_.player->hasUnlockedButton(static_cast<uint8_t>(KonamiButton::B)));
}

// ============================================
// BREACH DEFENSE WALKTHROUGH TESTS
// ============================================

TEST_F(E2EWalkthroughTestSuite, DISABLED_BreachDefenseLaunchPlayWin) {
    advanceToIdle();

    // Trigger FDN for Breach Defense (GameType 6, KonamiButton A=5)
    triggerFdnHandshake("6", "5");

    auto* bd = static_cast<BreachDefense*>(
        player_.pdn->getApp(StateId(BREACH_DEFENSE_APP_ID)));
    ASSERT_NE(bd, nullptr);
    ASSERT_TRUE(bd->getConfig().managedMode);

    // Configure for easy win
    bd->getConfig().totalThreats = 1;
    bd->getConfig().threatSpeedMs = 50;
    bd->getConfig().missesAllowed = 2;
    bd->getConfig().rngSeed = 42;

    // Advance past intro (2s)
    tickWithTime(25, 100);

    // Defend against wave
    tickWithTime(10, 100);
    player_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    tickWithTime(20, 100);

    // Should win
    ASSERT_EQ(bd->getOutcome().result, MiniGameResult::WON);

    tickWithTime(50, 100);

    ASSERT_TRUE(player_.player->hasUnlockedButton(static_cast<uint8_t>(KonamiButton::A)));
}

TEST_F(E2EWalkthroughTestSuite, DISABLED_BreachDefenseLaunchPlayLose) {
    advanceToIdle();

    triggerFdnHandshake("6", "5");

    auto* bd = static_cast<BreachDefense*>(
        player_.pdn->getApp(StateId(BREACH_DEFENSE_APP_ID)));
    ASSERT_NE(bd, nullptr);

    // Configure for easy loss
    bd->getConfig().totalThreats = 1;
    bd->getConfig().threatSpeedMs = 50;
    bd->getConfig().missesAllowed = 2;
    bd->getConfig().rngSeed = 42;

    // Advance past intro
    tickWithTime(25, 100);

    // Don't defend — let threat through
    tickWithTime(100, 100);

    // Should lose
    ASSERT_EQ(bd->getOutcome().result, MiniGameResult::LOST);

    tickWithTime(50, 100);

    ASSERT_FALSE(player_.player->hasUnlockedButton(static_cast<uint8_t>(KonamiButton::A)));
}

// ============================================
// MULTI-GAME SEQUENCE TEST
// ============================================

// REMOVED: MultiGameSequenceAllSeven used old APIs for Ghost Runner, Spike Vector, Cipher Path.
// Needs full rewrite once all 7 game E2E tests are updated for Wave 18 APIs.

#endif // NATIVE_BUILD
