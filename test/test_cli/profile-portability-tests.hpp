//
// Profile Portability Tests — Player state survives device reassignment
//

#pragma once

#include <gtest/gtest.h>
#include "game/player.hpp"
#include "device/device-types.hpp"
#include "game/quickdraw-requests.hpp"
#include "game/progress-manager.hpp"
#include "cli/cli-http-server.hpp"
#include "device/drivers/native/native-prefs-driver.hpp"
#include "device/drivers/native/native-http-client-driver.hpp"

// ============================================
// PROFILE PORTABILITY TEST SUITE
// ============================================

class ProfilePortabilityTestSuite : public testing::Test {
public:
    void SetUp() override {
        server_ = &cli::MockHttpServer::getInstance();
        server_->clearHistory();
        server_->setOffline(false);

        // Player A: has some Konami progress
        cli::MockPlayerConfig configA;
        configA.id = "0010";
        configA.name = "PlayerA";
        configA.isHunter = true;
        configA.allegiance = 1;
        configA.faction = "Guild";
        configA.konamiProgress = 5;  // UP + LEFT unlocked
        configA.equippedColorProfile = 7;  // SIGNAL_ECHO
        configA.colorProfileEligibility = {7};
        server_->configurePlayer("0010", configA);

        // Player B: fresh player, no progress
        cli::MockPlayerConfig configB;
        configB.id = "0020";
        configB.name = "PlayerB";
        configB.isHunter = false;
        configB.allegiance = 2;
        configB.faction = "Rebels";
        configB.konamiProgress = 0;
        configB.equippedColorProfile = 0;
        server_->configurePlayer("0020", configB);

        // Player C: advanced player with lots of progress
        cli::MockPlayerConfig configC;
        configC.id = "0030";
        configC.name = "PlayerC";
        configC.isHunter = true;
        configC.allegiance = 1;
        configC.faction = "Guild";
        configC.konamiProgress = 127;  // All buttons unlocked
        configC.equippedColorProfile = 7;
        configC.colorProfileEligibility = {7};
        server_->configurePlayer("0030", configC);
    }

    void TearDown() override {
        server_->clearHistory();
        server_->setOffline(false);
        server_->removePlayer("0010");
        server_->removePlayer("0020");
        server_->removePlayer("0030");
    }

    /*
     * Helper: simulate login — fetch player data from mock server and
     * apply it to the Player object (same logic as FetchUserData state).
     */
    void loginPlayer(Player* player, const std::string& playerId) {
        std::string response;
        int status = server_->handleRequest("GET", "/api/players/" + playerId, "", response);
        ASSERT_EQ(status, 200);

        PlayerResponse parsed;
        bool ok = parsed.parseFromJson(response);
        ASSERT_TRUE(ok);

        char* idBuf = new char[playerId.size() + 1];
        strcpy(idBuf, playerId.c_str());
        player->setUserID(idBuf);
        delete[] idBuf;

        player->setName(parsed.name.c_str());
        player->setIsHunter(parsed.isHunter);
        player->setAllegiance(parsed.allegiance);
        player->setFaction(parsed.faction.c_str());
        player->setKonamiProgress(parsed.konamiProgress);
        player->setEquippedColorProfile(
            static_cast<GameType>(parsed.equippedColorProfile));
        for (uint8_t g : parsed.colorProfileEligibility) {
            player->addColorProfileEligibility(static_cast<GameType>(g));
        }
    }

    cli::MockHttpServer* server_;
};

// Test: Login fetches complete profile including Konami progress
void portabilityLoginFetchesProfile(ProfilePortabilityTestSuite* suite) {
    Player player;
    suite->loginPlayer(&player, "0010");

    ASSERT_EQ(player.getName(), "PlayerA");
    ASSERT_EQ(player.getKonamiProgress(), 5);
    ASSERT_TRUE(player.hasUnlockedButton(KonamiButton::UP));
    ASSERT_TRUE(player.hasUnlockedButton(KonamiButton::LEFT));
    ASSERT_FALSE(player.hasUnlockedButton(KonamiButton::START));
    ASSERT_EQ(player.getEquippedColorProfile(), GameType::SIGNAL_ECHO);
    ASSERT_TRUE(player.hasColorProfileEligibility(GameType::SIGNAL_ECHO));
}

// Test: Same device, same player, same profile on re-login
void portabilityLoginSameDeviceSameProfile(ProfilePortabilityTestSuite* suite) {
    Player player;

    // First login
    suite->loginPlayer(&player, "0010");
    ASSERT_EQ(player.getKonamiProgress(), 5);

    // Simulate "restart" — create a fresh Player on same device
    Player player2;
    suite->loginPlayer(&player2, "0010");

    ASSERT_EQ(player2.getName(), "PlayerA");
    ASSERT_EQ(player2.getKonamiProgress(), 5);
    ASSERT_EQ(player2.getEquippedColorProfile(), GameType::SIGNAL_ECHO);
}

// Test: Different device, same player, same progress
void portabilityLoginDifferentDeviceSameProfile(ProfilePortabilityTestSuite* suite) {
    // Device 0: login as player A
    Player playerOnDevice0;
    suite->loginPlayer(&playerOnDevice0, "0010");
    ASSERT_EQ(playerOnDevice0.getKonamiProgress(), 5);

    // Device 1: login as same player A
    Player playerOnDevice1;
    suite->loginPlayer(&playerOnDevice1, "0010");

    // Same progress on different device
    ASSERT_EQ(playerOnDevice1.getKonamiProgress(), 5);
    ASSERT_EQ(playerOnDevice1.getEquippedColorProfile(), GameType::SIGNAL_ECHO);
    ASSERT_TRUE(playerOnDevice1.hasUnlockedButton(KonamiButton::UP));
    ASSERT_TRUE(playerOnDevice1.hasUnlockedButton(KonamiButton::LEFT));
}

// Test: Device reassignment — new player gets their own profile, not previous player's
void portabilityLoginNewProfileOnReassignedDevice(ProfilePortabilityTestSuite* suite) {
    // Login as Player A on device
    Player player;
    suite->loginPlayer(&player, "0010");
    ASSERT_EQ(player.getKonamiProgress(), 5);
    ASSERT_EQ(player.getEquippedColorProfile(), GameType::SIGNAL_ECHO);

    // "Reassign" device — login as Player B
    Player player2;
    suite->loginPlayer(&player2, "0020");

    // Player B has their own (empty) progress
    ASSERT_EQ(player2.getName(), "PlayerB");
    ASSERT_EQ(player2.getKonamiProgress(), 0);
    ASSERT_EQ(player2.getEquippedColorProfile(), GameType::QUICKDRAW);
    ASSERT_FALSE(player2.hasUnlockedButton(KonamiButton::UP));
}

// Test: Full booth scenario — check-in, play, check-out chain
void portabilityBoothCheckInOutChain(ProfilePortabilityTestSuite* suite) {
    // Player A checks in on device 0
    Player playerA_d0;
    suite->loginPlayer(&playerA_d0, "0010");
    ASSERT_EQ(playerA_d0.getKonamiProgress(), 5);

    // Player A "plays" (simulate unlocking another button locally)
    playerA_d0.unlockKonamiButton(KonamiButton::START);
    ASSERT_EQ(playerA_d0.getKonamiProgress(), 69);  // 5 + 64 = 69

    // Player A checks out. Player B checks in on same device.
    Player playerB_d0;
    suite->loginPlayer(&playerB_d0, "0020");
    ASSERT_EQ(playerB_d0.getName(), "PlayerB");
    ASSERT_EQ(playerB_d0.getKonamiProgress(), 0);

    // Player B plays, no progress changes.
    // Player B checks out. Player A checks in on device 1.
    // Server still has Player A's old progress (5, not 69 — upload hasn't happened yet).
    Player playerA_d1;
    suite->loginPlayer(&playerA_d1, "0010");
    ASSERT_EQ(playerA_d1.getKonamiProgress(), 5);  // Server has old progress
    ASSERT_EQ(playerA_d1.getName(), "PlayerA");
}

// Test: Offline fallback — load Konami progress from NVS when server unreachable
void portabilityOfflineFallbackToLocalProgress(ProfilePortabilityTestSuite* suite) {
    // First: save progress to NVS (simulating a previous session)
    NativePrefsDriver storage("TestPortabilityStorage");
    Player player("0010", Allegiance::RESISTANCE, true);
    ProgressManager pm;
    pm.initialize(&player, &storage);

    player.setKonamiProgress(21);  // UP + LEFT + B
    pm.saveProgress();

    // Now simulate offline login — server unreachable
    Player offlinePlayer("0010", Allegiance::RESISTANCE, true);
    ProgressManager offlinePM;
    offlinePM.initialize(&offlinePlayer, &storage);

    // Player starts with 0 progress (no server fetch)
    ASSERT_EQ(offlinePlayer.getKonamiProgress(), 0);

    // Load from NVS fallback
    offlinePM.loadProgress();

    ASSERT_EQ(offlinePlayer.getKonamiProgress(), 21);
    ASSERT_TRUE(offlinePlayer.hasUnlockedButton(KonamiButton::UP));
    ASSERT_TRUE(offlinePlayer.hasUnlockedButton(KonamiButton::LEFT));
    ASSERT_TRUE(offlinePlayer.hasUnlockedButton(KonamiButton::B));
}

// Test: Progress survives device restart via NVS persistence
void portabilityProgressSurvivesRestart(ProfilePortabilityTestSuite* suite) {
    // Session 1: Player logs in, unlocks a button, progress saved to NVS
    NativePrefsDriver storage("TestRestartStorage");
    Player player1("0010", Allegiance::RESISTANCE, true);
    ProgressManager pm1;
    pm1.initialize(&player1, &storage);

    suite->loginPlayer(&player1, "0010");  // Gets progress=5 from server
    player1.unlockKonamiButton(KonamiButton::START);  // Now 69
    pm1.saveProgress();

    // Verify NVS has updated progress
    uint8_t stored = storage.readUChar("konami", 0);
    ASSERT_EQ(stored, 69);

    // Session 2: "Device restart" — new Player and ProgressManager, same storage
    Player player2("0010", Allegiance::RESISTANCE, true);
    ProgressManager pm2;
    pm2.initialize(&player2, &storage);

    // Before server fetch, load from NVS
    pm2.loadProgress();
    ASSERT_EQ(player2.getKonamiProgress(), 69);

    // Verify the new progress includes both server-fetched and locally-earned buttons
    ASSERT_TRUE(player2.hasUnlockedButton(KonamiButton::UP));     // from server (bit 0)
    ASSERT_TRUE(player2.hasUnlockedButton(KonamiButton::LEFT));   // from server (bit 2)
    ASSERT_TRUE(player2.hasUnlockedButton(KonamiButton::START));  // earned locally (bit 6)
}
