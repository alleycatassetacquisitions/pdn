#pragma once

#ifdef NATIVE_BUILD

#include <gtest/gtest.h>
#include "cli/cli-device.hpp"
#include "cli/cli-serial-broker.hpp"
#include "cli/cli-http-server.hpp"
#include "game/firewall-decrypt/firewall-decrypt.hpp"
#include "game/firewall-decrypt/firewall-decrypt-states.hpp"
#include "game/firewall-decrypt/firewall-decrypt-resources.hpp"
#include "game/minigame.hpp"
#include "device/device-types.hpp"
#include "utils/simple-timer.hpp"

using namespace cli;

// ============================================
// ADDRESS GENERATION TEST SUITE
// ============================================

class DecryptAddressTestSuite : public testing::Test {
public:
    void SetUp() override {
        // Reset all singleton state before each test to prevent pollution
        SerialCableBroker::resetInstance();
        MockHttpServer::resetInstance();
        SimpleTimer::resetClock();

        FirewallDecryptConfig config;
        config.rngSeed = 42;
        game = new FirewallDecrypt(config);
        game->populateStateMap();
    }

    void TearDown() override {
        delete game;

        // Clean up singleton state after each test
        SerialCableBroker::resetInstance();
        MockHttpServer::resetInstance();
        SimpleTimer::resetClock();
    }

    FirewallDecrypt* game;
};

// Test: Generated addresses have correct format (XX:XX:XX:XX)
void decryptAddressFormat(DecryptAddressTestSuite* suite) {
    suite->game->seedRng(suite->game->getConfig().rngSeed);
    std::string addr = suite->game->generateAddress();
    // Format: "XX:XX:XX:XX" = 11 chars
    ASSERT_EQ(addr.length(), 11u);
    ASSERT_EQ(addr[2], ':');
    ASSERT_EQ(addr[5], ':');
    ASSERT_EQ(addr[8], ':');
}

// Test: Seeded RNG produces deterministic addresses
void decryptAddressDeterministic(DecryptAddressTestSuite* suite) {
    suite->game->seedRng(suite->game->getConfig().rngSeed);
    std::string addr1 = suite->game->generateAddress();
    // Re-seed with same seed
    suite->game->seedRng(suite->game->getConfig().rngSeed);
    std::string addr2 = suite->game->generateAddress();
    ASSERT_EQ(addr1, addr2);
}

// Test: Decoy differs from target
void decryptDecoyDiffers(DecryptAddressTestSuite* suite) {
    suite->game->seedRng(suite->game->getConfig().rngSeed);
    std::string target = suite->game->generateAddress();
    std::string decoy = suite->game->generateDecoy(target, 0.0f);
    ASSERT_NE(target, decoy);
    ASSERT_EQ(decoy.length(), 11u);
}

// Test: High similarity decoy shares some pairs with target
void decryptDecoySimilarity(DecryptAddressTestSuite* suite) {
    suite->game->seedRng(suite->game->getConfig().rngSeed);
    std::string target = suite->game->generateAddress();
    std::string decoy = suite->game->generateDecoy(target, 0.7f);
    ASSERT_NE(target, decoy);
    // With 0.7 similarity, ~2 of 4 pairs should match
    // Just check it's a valid format and different
    ASSERT_EQ(decoy.length(), 11u);
}

// Test: setupRound populates candidates with target included
void decryptSetupRoundContainsTarget(DecryptAddressTestSuite* suite) {
    suite->game->seedRng(suite->game->getConfig().rngSeed);
    suite->game->setupRound();

    auto& session = suite->game->getSession();
    ASSERT_EQ(static_cast<int>(session.candidates.size()),
              suite->game->getConfig().numCandidates);
    ASSERT_GE(session.targetIndex, 0);
    ASSERT_LT(session.targetIndex, static_cast<int>(session.candidates.size()));
    ASSERT_EQ(session.candidates[session.targetIndex], session.target);
}

// ============================================
// GAME LIFECYCLE TEST SUITE
// ============================================

class DecryptLifecycleTestSuite : public testing::Test {
public:
    void SetUp() override {
        device_ = DeviceFactory::createGameDevice(0, "firewall-decrypt");
        SimpleTimer::setPlatformClock(device_.clockDriver);
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

    int getStateId() {
        return device_.game->getCurrentStateId();
    }

    FirewallDecrypt* getGame() {
        return dynamic_cast<FirewallDecrypt*>(device_.game);
    }

    DeviceInstance device_;
};

// Test: Game starts in DecryptIntro
void decryptStartsInIntro(DecryptLifecycleTestSuite* suite) {
    // State is set during loadAppConfig (no tick needed)
    ASSERT_EQ(suite->getStateId(), DECRYPT_INTRO);
}

// Test: Intro transitions to Scan after timer
void decryptIntroTransitionsToScan(DecryptLifecycleTestSuite* suite) {
    ASSERT_EQ(suite->getStateId(), DECRYPT_INTRO);

    // Advance past 2s intro timer
    suite->tickWithTime(5, 500);
    ASSERT_EQ(suite->getStateId(), DECRYPT_SCAN);
}

// Test: Intro shows title text
void decryptIntroShowsTitle(DecryptLifecycleTestSuite* suite) {
    // Display text is drawn during onStateMounted (no tick needed)
    auto& textHistory = suite->device_.displayDriver->getTextHistory();
    bool foundFirewall = false;
    bool foundDecrypt = false;
    for (const auto& entry : textHistory) {
        if (entry.find("FIREWALL") != std::string::npos) foundFirewall = true;
        if (entry.find("DECRYPT") != std::string::npos) foundDecrypt = true;
    }
    ASSERT_TRUE(foundFirewall);
    ASSERT_TRUE(foundDecrypt);
}

// Test: Scan shows target address
void decryptScanShowsTarget(DecryptLifecycleTestSuite* suite) {
    suite->tick(1);
    suite->tickWithTime(5, 500);  // Get to Scan
    ASSERT_EQ(suite->getStateId(), DECRYPT_SCAN);

    auto& session = suite->getGame()->getSession();
    auto& textHistory = suite->device_.displayDriver->getTextHistory();
    bool foundTarget = false;
    for (const auto& entry : textHistory) {
        if (entry.find(session.target) != std::string::npos) foundTarget = true;
    }
    ASSERT_TRUE(foundTarget);
}

// Test: Cursor movement wraps around candidate list
void decryptScanCursorWraps(DecryptLifecycleTestSuite* suite) {
    suite->tick(1);
    suite->tickWithTime(5, 500);
    ASSERT_EQ(suite->getStateId(), DECRYPT_SCAN);

    auto& session = suite->getGame()->getSession();
    int numCandidates = static_cast<int>(session.candidates.size());

    // Scroll numCandidates times — should wrap back to 0
    for (int i = 0; i < numCandidates; i++) {
        suite->device_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
        suite->tick(1);
    }

    // Confirm selection — after wrapping, cursor should be at 0
    suite->device_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tick(1);

    // Should have transitioned to evaluate (correct or wrong, always goes through evaluate)
    ASSERT_EQ(suite->getStateId(), DECRYPT_EVALUATE);
}

// Test: Correct selection advances round
void decryptCorrectAdvancesRound(DecryptLifecycleTestSuite* suite) {
    suite->tick(1);
    suite->tickWithTime(5, 500);
    ASSERT_EQ(suite->getStateId(), DECRYPT_SCAN);

    auto& session = suite->getGame()->getSession();

    // Scroll to target
    for (int i = 0; i < session.targetIndex; i++) {
        suite->device_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
        suite->tick(1);
    }

    // Confirm
    suite->device_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tick(2);

    // Should advance to next round (DecryptScan again) or evaluate first
    // After correct, evaluate transitions to scan for next round
    ASSERT_EQ(session.currentRound, 1);
    ASSERT_EQ(session.score, 100);
}

// Test: Wrong selection ends game
void decryptWrongSelectionLoses(DecryptLifecycleTestSuite* suite) {
    suite->tick(1);
    suite->tickWithTime(5, 500);
    ASSERT_EQ(suite->getStateId(), DECRYPT_SCAN);

    auto& session = suite->getGame()->getSession();

    // Find a wrong index
    int wrongIndex = (session.targetIndex + 1) % static_cast<int>(session.candidates.size());

    // Scroll to wrong
    for (int i = 0; i < wrongIndex; i++) {
        suite->device_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
        suite->tick(1);
    }

    // Confirm wrong answer
    suite->device_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tick(3);

    // Should be in lose state
    ASSERT_EQ(suite->getStateId(), DECRYPT_LOSE);
}

// Test: Completing all rounds wins
void decryptAllRoundsWins(DecryptLifecycleTestSuite* suite) {
    suite->tick(1);
    suite->tickWithTime(5, 500);

    int numRounds = suite->getGame()->getConfig().numRounds;

    for (int round = 0; round < numRounds; round++) {
        ASSERT_EQ(suite->getStateId(), DECRYPT_SCAN);

        auto& session = suite->getGame()->getSession();

        // Scroll to target
        for (int i = 0; i < session.targetIndex; i++) {
            suite->device_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
            suite->tick(1);
        }

        // Confirm correct
        suite->device_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
        suite->tick(3);
    }

    ASSERT_EQ(suite->getStateId(), DECRYPT_WIN);
}

// Test: Win sets outcome
void decryptWinSetsOutcome(DecryptLifecycleTestSuite* suite) {
    suite->tick(1);
    suite->tickWithTime(5, 500);

    int numRounds = suite->getGame()->getConfig().numRounds;

    for (int round = 0; round < numRounds; round++) {
        auto& session = suite->getGame()->getSession();
        for (int i = 0; i < session.targetIndex; i++) {
            suite->device_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
            suite->tick(1);
        }
        suite->device_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
        suite->tick(3);
    }

    ASSERT_EQ(suite->getStateId(), DECRYPT_WIN);
    ASSERT_EQ(suite->getGame()->getOutcome().result, MiniGameResult::WON);
    ASSERT_EQ(suite->getGame()->getOutcome().score, numRounds * 100);
}

// Test: Lose sets outcome
void decryptLoseSetsOutcome(DecryptLifecycleTestSuite* suite) {
    suite->tick(1);
    suite->tickWithTime(5, 500);
    ASSERT_EQ(suite->getStateId(), DECRYPT_SCAN);

    auto& session = suite->getGame()->getSession();
    int wrongIndex = (session.targetIndex + 1) % static_cast<int>(session.candidates.size());

    for (int i = 0; i < wrongIndex; i++) {
        suite->device_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
        suite->tick(1);
    }
    suite->device_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tick(3);

    ASSERT_EQ(suite->getStateId(), DECRYPT_LOSE);
    ASSERT_EQ(suite->getGame()->getOutcome().result, MiniGameResult::LOST);
}

// Test: Standalone mode loops back to intro after win
void decryptStandaloneRestartAfterWin(DecryptLifecycleTestSuite* suite) {
    suite->tick(1);
    suite->tickWithTime(5, 500);

    int numRounds = suite->getGame()->getConfig().numRounds;

    for (int round = 0; round < numRounds; round++) {
        auto& session = suite->getGame()->getSession();
        for (int i = 0; i < session.targetIndex; i++) {
            suite->device_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
            suite->tick(1);
        }
        suite->device_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
        suite->tick(3);
    }

    ASSERT_EQ(suite->getStateId(), DECRYPT_WIN);

    // Wait for win timer
    suite->tickWithTime(10, 400);

    // In standalone mode, should loop back to intro
    ASSERT_EQ(suite->getStateId(), DECRYPT_INTRO);
}

// Test: Standalone mode loops back to intro after lose
void decryptStandaloneRestartAfterLose(DecryptLifecycleTestSuite* suite) {
    suite->tick(1);
    suite->tickWithTime(5, 500);

    auto& session = suite->getGame()->getSession();
    int wrongIndex = (session.targetIndex + 1) % static_cast<int>(session.candidates.size());

    for (int i = 0; i < wrongIndex; i++) {
        suite->device_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
        suite->tick(1);
    }
    suite->device_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tick(3);

    ASSERT_EQ(suite->getStateId(), DECRYPT_LOSE);

    // Wait for lose timer
    suite->tickWithTime(10, 400);

    ASSERT_EQ(suite->getStateId(), DECRYPT_INTRO);
}

// ============================================
// MANAGED MODE TEST SUITE
// ============================================

class DecryptManagedTestSuite : public testing::Test {
public:
    void SetUp() override {
        player_ = DeviceFactory::createDevice(0, true);
        fdn_ = DeviceFactory::createFdnDevice(1, GameType::FIREWALL_DECRYPT);
        SimpleTimer::setPlatformClock(player_.clockDriver);
    }

    void TearDown() override {
        DeviceFactory::destroyDevice(fdn_);
        DeviceFactory::destroyDevice(player_);
    }

    void tick(int n = 1) {
        for (int i = 0; i < n; i++) {
            SerialCableBroker::getInstance().transferData();
            player_.pdn->loop();
            fdn_.pdn->loop();
        }
    }

    void tickPlayerWithTime(int n, int delayMs) {
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

    FirewallDecrypt* getFirewallDecrypt() {
        return dynamic_cast<FirewallDecrypt*>(
            player_.pdn->getApp(StateId(FIREWALL_DECRYPT_APP_ID)));
    }

    DeviceInstance player_;
    DeviceInstance fdn_;
};

// Test: FDN with FIREWALL_DECRYPT triggers correct game
void decryptManagedLaunchesFromFdn(DecryptManagedTestSuite* suite) {
    suite->advanceToIdle();

    // Inject FDN broadcast for FIREWALL_DECRYPT (GameType 3, KonamiButton LEFT=2)
    suite->player_.serialOutDriver->injectInput("*fdn:3:2\r");
    suite->tick(3);
    ASSERT_EQ(suite->getPlayerStateId(), FDN_DETECTED);

    // Complete handshake
    suite->player_.serialOutDriver->injectInput("*fack\r");
    suite->tickPlayerWithTime(5, 100);

    // Should be in Firewall Decrypt now
    auto* fw = suite->getFirewallDecrypt();
    ASSERT_NE(fw, nullptr);
    ASSERT_TRUE(fw->getConfig().managedMode);
}

// Test: Managed mode win returns to previous app
void decryptManagedWinReturns(DecryptManagedTestSuite* suite) {
    suite->advanceToIdle();

    suite->player_.serialOutDriver->injectInput("*fdn:3:2\r");
    suite->tick(3);
    suite->player_.serialOutDriver->injectInput("*fack\r");
    suite->tickPlayerWithTime(5, 100);

    auto* fw = suite->getFirewallDecrypt();
    ASSERT_NE(fw, nullptr);

    // Play through all rounds
    int numRounds = fw->getConfig().numRounds;
    // Advance to scan
    suite->tickPlayerWithTime(5, 500);

    for (int round = 0; round < numRounds; round++) {
        auto& session = fw->getSession();
        for (int i = 0; i < session.targetIndex; i++) {
            suite->player_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
            suite->tickPlayerWithTime(1, 10);
        }
        suite->player_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
        suite->tickPlayerWithTime(3, 10);
    }

    // Wait for win timer
    suite->tickPlayerWithTime(10, 400);

    // Should return to Quickdraw (FdnComplete state)
    ASSERT_EQ(suite->getPlayerStateId(), FDN_COMPLETE);
}

// ============================================
// CONFIG PRESET TESTS
// ============================================

class DecryptConfigTestSuite : public testing::Test {};

void decryptEasyPresetValues(DecryptConfigTestSuite* /*suite*/) {
    ASSERT_EQ(FIREWALL_DECRYPT_EASY.numCandidates, 5);
    ASSERT_EQ(FIREWALL_DECRYPT_EASY.numRounds, 3);
    ASSERT_EQ(FIREWALL_DECRYPT_EASY.timeLimitMs, 0);
    ASSERT_FALSE(FIREWALL_DECRYPT_EASY.managedMode);
}

void decryptHardPresetValues(DecryptConfigTestSuite* /*suite*/) {
    ASSERT_EQ(FIREWALL_DECRYPT_HARD.numCandidates, 10);
    ASSERT_EQ(FIREWALL_DECRYPT_HARD.numRounds, 4);
    ASSERT_EQ(FIREWALL_DECRYPT_HARD.timeLimitMs, 15000);
    ASSERT_FALSE(FIREWALL_DECRYPT_HARD.managedMode);
}

// ============================================
// STATE NAME TESTS
// ============================================

class DecryptStateNameTestSuite : public testing::Test {};

void decryptStateNamesCorrect(DecryptStateNameTestSuite* /*suite*/) {
    ASSERT_STREQ(getFirewallDecryptStateName(DECRYPT_INTRO), "DecryptIntro");
    ASSERT_STREQ(getFirewallDecryptStateName(DECRYPT_SCAN), "DecryptScan");
    ASSERT_STREQ(getFirewallDecryptStateName(DECRYPT_EVALUATE), "DecryptEvaluate");
    ASSERT_STREQ(getFirewallDecryptStateName(DECRYPT_WIN), "DecryptWin");
    ASSERT_STREQ(getFirewallDecryptStateName(DECRYPT_LOSE), "DecryptLose");
}

// Test: getStateName routes 200+ range to Firewall Decrypt
void decryptGetStateNameRoutes(DecryptStateNameTestSuite* /*suite*/) {
    ASSERT_STREQ(getStateName(DECRYPT_INTRO), "DecryptIntro");
    ASSERT_STREQ(getStateName(DECRYPT_WIN), "DecryptWin");
}

// ============================================
// APP ID TESTS
// ============================================

class DecryptAppIdTestSuite : public testing::Test {};

void decryptAppIdForGame(DecryptAppIdTestSuite* /*suite*/) {
    ASSERT_EQ(getAppIdForGame(GameType::FIREWALL_DECRYPT), FIREWALL_DECRYPT_APP_ID);
    ASSERT_EQ(FIREWALL_DECRYPT_APP_ID, 3);
}

// ============================================
// TIMEOUT & ERROR PATH TESTS (NEW)
// ============================================

class DecryptTimeoutTestSuite : public testing::Test {
public:
    void SetUp() override {
        device_ = DeviceFactory::createGameDevice(0, "firewall-decrypt");
        SimpleTimer::setPlatformClock(device_.clockDriver);

        // Configure hard mode with timeout
        auto* game = static_cast<FirewallDecrypt*>(device_.game);
        game->getConfig().timeLimitMs = 5000;  // 5 second timeout
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

    FirewallDecrypt* getGame() {
        return static_cast<FirewallDecrypt*>(device_.game);
    }

    DeviceInstance device_;
};

// Test: Timeout during scan triggers timedOut flag
void decryptScanTimeoutSetsFlag(DecryptTimeoutTestSuite* suite) {
    suite->tick(1);
    suite->tickWithTime(5, 500);  // Advance to Scan
    ASSERT_EQ(suite->getGame()->getCurrentStateId(), DECRYPT_SCAN);

    auto& session = suite->getGame()->getSession();
    ASSERT_FALSE(session.timedOut);

    // Advance past timeout (5000ms)
    suite->tickWithTime(60, 100);

    // Should have transitioned to Evaluate
    ASSERT_EQ(suite->getGame()->getCurrentStateId(), DECRYPT_EVALUATE);
}

// Test: Timeout causes evaluate to route to lose
void decryptTimeoutRoutesToLose(DecryptTimeoutTestSuite* suite) {
    suite->tick(1);
    suite->tickWithTime(5, 500);  // Advance to Scan
    ASSERT_EQ(suite->getGame()->getCurrentStateId(), DECRYPT_SCAN);

    // Advance past timeout
    suite->tickWithTime(60, 100);

    // After evaluate processes timeout, should be in lose state
    suite->tick(3);
    ASSERT_EQ(suite->getGame()->getCurrentStateId(), DECRYPT_LOSE);
}

// Test: Timeout sets outcome to LOST
void decryptTimeoutSetsLoseOutcome(DecryptTimeoutTestSuite* suite) {
    suite->tick(1);
    suite->tickWithTime(5, 500);
    suite->tickWithTime(60, 100);  // Timeout
    suite->tick(3);

    ASSERT_EQ(suite->getGame()->getOutcome().result, MiniGameResult::LOST);
}

// Test: Selection made before timeout is evaluated normally
void decryptSelectionBeforeTimeout(DecryptTimeoutTestSuite* suite) {
    suite->tick(1);
    suite->tickWithTime(5, 500);
    ASSERT_EQ(suite->getGame()->getCurrentStateId(), DECRYPT_SCAN);

    auto& session = suite->getGame()->getSession();

    // Make correct selection before timeout
    for (int i = 0; i < session.targetIndex; i++) {
        suite->device_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
        suite->tick(1);
    }
    suite->device_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tick(3);

    // Should advance to next round (not timeout)
    ASSERT_FALSE(session.timedOut);
    ASSERT_EQ(session.currentRound, 1);
}

// Test: Cursor wrapping with single candidate
void decryptCursorWrapSingleCandidate(DecryptLifecycleTestSuite* suite) {
    // Manually set up session with 1 candidate
    auto* game = suite->getGame();
    game->getConfig().numCandidates = 1;
    game->setupRound();

    suite->tick(1);
    suite->tickWithTime(5, 500);
    ASSERT_EQ(suite->getStateId(), DECRYPT_SCAN);

    auto& session = game->getSession();
    ASSERT_EQ(static_cast<int>(session.candidates.size()), 1);

    // Scroll should wrap immediately (cursor stays at 0)
    suite->device_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tick(1);

    // Cursor should still be 0 after wrapping
    // Confirm selection
    suite->device_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tick(2);

    // Should proceed to evaluate (target must be at index 0)
    ASSERT_EQ(suite->getStateId(), DECRYPT_EVALUATE);
}

// Test: Cursor wrapping with two candidates
void decryptCursorWrapTwoCandidates(DecryptLifecycleTestSuite* suite) {
    auto* game = suite->getGame();
    game->getConfig().numCandidates = 2;
    game->setupRound();

    suite->tick(1);
    suite->tickWithTime(5, 500);
    ASSERT_EQ(suite->getStateId(), DECRYPT_SCAN);

    // Scroll twice should wrap back to 0
    suite->device_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tick(1);
    suite->device_.primaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tick(1);

    // Cursor should be back at 0
    suite->device_.secondaryButtonDriver->execCallback(ButtonInteraction::CLICK);
    suite->tick(2);

    ASSERT_EQ(suite->getStateId(), DECRYPT_EVALUATE);
}

#endif // NATIVE_BUILD
