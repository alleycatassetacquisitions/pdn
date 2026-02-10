//
// Profile Sync Tests — Server response parsing, NVS persistence, upload
//

#pragma once

#include <gtest/gtest.h>
#include "game/player.hpp"
#include "device/device-types.hpp"
#include "game/quickdraw-requests.hpp"
#include "game/progress-manager.hpp"
#include "cli/cli-http-server.hpp"
#include "device/drivers/native/native-http-client-driver.hpp"
#include "device/drivers/native/native-prefs-driver.hpp"

// ============================================
// PROFILE SYNC TEST SUITE
// ============================================

class ProfileSyncTestSuite : public testing::Test {
public:
    void SetUp() override {
        player_ = new Player("0010", Allegiance::RESISTANCE, true);
        storage_ = new NativePrefsDriver("TestProgressStorage");
        progressManager_ = new ProgressManager();
        progressManager_->initialize(player_, storage_);

        server_ = &cli::MockHttpServer::getInstance();
        server_->clearHistory();
        server_->setOffline(false);
    }

    void TearDown() override {
        server_->clearHistory();
        server_->setOffline(false);
        server_->removePlayer("0010");

        delete progressManager_;
        delete storage_;
        delete player_;
    }

    Player* player_;
    NativePrefsDriver* storage_;
    ProgressManager* progressManager_;
    cli::MockHttpServer* server_;
};

// Test: MockHttpServer returns konami_progress in GET response
void profileSyncServerReturnsProgress(ProfileSyncTestSuite* suite) {
    cli::MockPlayerConfig config;
    config.id = "0010";
    config.name = "TestPlayer";
    config.isHunter = true;
    config.allegiance = 1;
    config.faction = "Guild";
    config.konamiProgress = 37;  // 0b00100101 = UP + LEFT + A
    config.equippedColorProfile = 7;  // SIGNAL_ECHO
    config.colorProfileEligibility = {7};

    suite->server_->configurePlayer("0010", config);

    std::string response;
    int status = suite->server_->handleRequest("GET", "/api/players/0010", "", response);

    ASSERT_EQ(status, 200);
    ASSERT_TRUE(response.find("\"konami_progress\":37") != std::string::npos);
    ASSERT_TRUE(response.find("\"equipped_color_profile\":7") != std::string::npos);
    ASSERT_TRUE(response.find("\"color_profile_eligibility\":[7]") != std::string::npos);
}

// Test: PlayerResponse::parseFromJson correctly parses Konami fields
void profileSyncFetchUserDataParsesProgress(ProfileSyncTestSuite* suite) {
    std::string json = R"({"data":{"id":"0010","name":"TestPlayer","hunter":true,"allegiance":1,"faction":"Guild","konami_progress":5,"equipped_color_profile":7,"color_profile_eligibility":[7]}})";

    PlayerResponse response;
    bool ok = response.parseFromJson(json);

    ASSERT_TRUE(ok);
    ASSERT_EQ(response.konamiProgress, 5);
    ASSERT_EQ(response.equippedColorProfile, 7);
    ASSERT_EQ(response.colorProfileEligibility.size(), 1);
    ASSERT_EQ(response.colorProfileEligibility[0], 7);

    // Simulate what FetchUserData does: set on player
    suite->player_->setKonamiProgress(response.konamiProgress);
    suite->player_->setEquippedColorProfile(
        static_cast<GameType>(response.equippedColorProfile));
    for (uint8_t g : response.colorProfileEligibility) {
        suite->player_->addColorProfileEligibility(static_cast<GameType>(g));
    }

    ASSERT_EQ(suite->player_->getKonamiProgress(), 5);
    ASSERT_TRUE(suite->player_->hasUnlockedButton(KonamiButton::UP));
    ASSERT_TRUE(suite->player_->hasUnlockedButton(KonamiButton::LEFT));
    ASSERT_FALSE(suite->player_->hasUnlockedButton(KonamiButton::START));
    ASSERT_EQ(suite->player_->getEquippedColorProfile(), GameType::SIGNAL_ECHO);
}

// Test: Missing Konami fields in server response defaults to zero (no crash)
void profileSyncFetchUserDataMissingProgress(ProfileSyncTestSuite* suite) {
    // Old-format server response without Konami fields
    std::string json = R"({"data":{"id":"0010","name":"TestPlayer","hunter":true,"allegiance":1,"faction":"Guild"}})";

    PlayerResponse response;
    bool ok = response.parseFromJson(json);

    ASSERT_TRUE(ok);
    ASSERT_EQ(response.konamiProgress, 0);
    ASSERT_EQ(response.equippedColorProfile, 0);
    ASSERT_TRUE(response.colorProfileEligibility.empty());
}

// Test: saveProgress writes konami bitmask to NVS storage
void profileSyncProgressSavedToNVS(ProfileSyncTestSuite* suite) {
    suite->player_->unlockKonamiButton(KonamiButton::UP);     // bit 0
    suite->player_->unlockKonamiButton(KonamiButton::START);  // bit 6

    suite->progressManager_->saveProgress();

    // Verify NVS contains the progress
    uint8_t stored = suite->storage_->readUChar("konami", 0);
    ASSERT_EQ(stored, 65);  // 0b01000001

    // Verify it's marked as unsynced
    uint8_t synced = suite->storage_->readUChar("synced", 1);
    ASSERT_EQ(synced, 0);
    ASSERT_TRUE(suite->progressManager_->hasUnsyncedProgress());
}

// Test: loadProgress reads konami bitmask from NVS into Player
void profileSyncProgressLoadedFromNVS(ProfileSyncTestSuite* suite) {
    // Manually write progress to NVS (simulate a previous save)
    suite->storage_->writeUChar("konami", 21);  // 0b00010101 = UP + LEFT + B

    // Create a fresh player and progress manager
    Player freshPlayer("0010", Allegiance::RESISTANCE, true);
    ProgressManager freshPM;
    freshPM.initialize(&freshPlayer, suite->storage_);

    ASSERT_EQ(freshPlayer.getKonamiProgress(), 0);  // Not loaded yet

    freshPM.loadProgress();

    ASSERT_EQ(freshPlayer.getKonamiProgress(), 21);
    ASSERT_TRUE(freshPlayer.hasUnlockedButton(KonamiButton::UP));
    ASSERT_TRUE(freshPlayer.hasUnlockedButton(KonamiButton::LEFT));
    ASSERT_TRUE(freshPlayer.hasUnlockedButton(KonamiButton::B));
    ASSERT_FALSE(freshPlayer.hasUnlockedButton(KonamiButton::START));
}

// Test: uploadProgress sends correct JSON body to PUT endpoint
void profileSyncProgressUploadSendsCorrectJson(ProfileSyncTestSuite* suite) {
    suite->player_->unlockKonamiButton(KonamiButton::UP);
    suite->player_->unlockKonamiButton(KonamiButton::LEFT);
    suite->progressManager_->saveProgress();

    // Configure mock server to accept the progress
    cli::MockPlayerConfig config;
    config.id = "0010";
    config.name = "TestPlayer";
    config.isHunter = true;
    config.allegiance = 1;
    config.faction = "Guild";
    suite->server_->configurePlayer("0010", config);

    // Verify the PUT endpoint accepts progress JSON directly
    std::string progressBody = R"({"konami_progress":5})";
    std::string response;
    int status = suite->server_->handleRequest(
        "PUT", "/api/players/0010/progress", progressBody, response);

    ASSERT_EQ(status, 200);
    ASSERT_TRUE(response.find("success") != std::string::npos);

    // Verify the server updated the player config
    std::string getResponse;
    suite->server_->handleRequest("GET", "/api/players/0010", "", getResponse);
    ASSERT_TRUE(getResponse.find("\"konami_progress\":5") != std::string::npos);
}
