#pragma once

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <cstring>
#include <string>
#include <vector>

#include "device-mock.hpp"
#include "game/player.hpp"
#include "wireless/page-cmd.hpp"
#include "wireless/page-manager.hpp"

using ::testing::_;
using ::testing::Return;
using ::testing::ReturnNull;

inline const uint8_t* pageJson(const std::string& json, std::vector<uint8_t>& storage) {
    storage.assign(json.begin(), json.end());
    return storage.data();
}

TEST(PageCmdParseTest, parsesFields) {
    std::vector<uint8_t> buf;
    const char* raw =
        R"({"id":"abc","text":"Hello","duration":7,"scroll":true,"player_id":"p1","role":"hunter","neocorp":"helix"})";
    PageCmd cmd;
    ASSERT_TRUE(parsePageCmdJson(pageJson(raw, buf), buf.size(), cmd));
    EXPECT_EQ(cmd.id, "abc");
    EXPECT_EQ(cmd.text, "Hello");
    EXPECT_EQ(cmd.durationSeconds, 7);
    EXPECT_TRUE(cmd.scroll);
    EXPECT_EQ(cmd.playerId, "p1");
    EXPECT_EQ(cmd.role, "hunter");
    EXPECT_EQ(cmd.neocorp, "helix");
}

TEST(PageCmdFilterTest, emptyStampsAccept) {
    Player player("1234", Allegiance::HELIX, true);
    PageCmd cmd;
    cmd.text = "hi";
    EXPECT_TRUE(pageCmdMatchesPlayer(cmd, &player));
}

TEST(PageCmdFilterTest, roleAndNeocorpMustMatch) {
    Player hunter("1234", Allegiance::HELIX, true);
    Player bounty("5678", Allegiance::ENDLINE, false);

    PageCmd hunterHelix;
    hunterHelix.role = "hunter";
    hunterHelix.neocorp = "helix";
    EXPECT_TRUE(pageCmdMatchesPlayer(hunterHelix, &hunter));
    EXPECT_FALSE(pageCmdMatchesPlayer(hunterHelix, &bounty));

    PageCmd bountyEndline;
    bountyEndline.role = "bounty";
    bountyEndline.neocorp = "endline";
    EXPECT_TRUE(pageCmdMatchesPlayer(bountyEndline, &bounty));
    EXPECT_FALSE(pageCmdMatchesPlayer(bountyEndline, &hunter));
}

TEST(PageCmdFilterTest, playerIdMatchesUserId) {
    Player player("p9", Allegiance::ALLEYCAT, false);
    PageCmd cmd;
    cmd.playerId = "p9";
    EXPECT_TRUE(pageCmdMatchesPlayer(cmd, &player));
    cmd.playerId = "other";
    EXPECT_FALSE(pageCmdMatchesPlayer(cmd, &player));
}

class PageManagerTestSuite : public ::testing::Test {
protected:
    void SetUp() override {
        device = new MockDevice();
        ON_CALL(*device->mockPeerComms, getPeerCommsState())
            .WillByDefault(Return(PeerCommsState::CONNECTED));
        ON_CALL(*device->mockPeerComms, getGlobalBroadcastAddress())
            .WillByDefault(Return(broadcastMac));
        ON_CALL(*device->mockPeerComms, sendData(_, _, _, _)).WillByDefault(Return(0));
        ON_CALL(*device->mockHttpClient, getHttpClientState())
            .WillByDefault(Return(HttpClientState::DISCONNECTED));
        device->getWirelessManager()->initialize();

        player = new Player("p1", Allegiance::HELIX, true);
        pageManager.initialize(device->getWirelessManager(), player);
    }

    void TearDown() override {
        delete player;
        player = nullptr;
        delete device;
        device = nullptr;
    }

    uint8_t broadcastMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    MockDevice* device = nullptr;
    Player* player = nullptr;
    PageManager pageManager;
};

TEST_F(PageManagerTestSuite, processPacketQueuesPendingAndDedupes) {
    std::vector<uint8_t> buf;
    const char* raw = R"({"id":"page-1","text":"Ping","duration":3})";
    ASSERT_EQ(pageManager.processPacket(nullptr, pageJson(raw, buf), buf.size()), 0);
    ASSERT_TRUE(pageManager.hasPendingPage());
    EXPECT_EQ(pageManager.pendingPage().text, "Ping");
    pageManager.clearPendingPage();
    EXPECT_FALSE(pageManager.hasPendingPage());

    EXPECT_EQ(pageManager.processPacket(nullptr, pageJson(raw, buf), buf.size()), 0);
    EXPECT_FALSE(pageManager.hasPendingPage());
}

TEST_F(PageManagerTestSuite, processPacketFiltersNonMatchingRole) {
    std::vector<uint8_t> buf;
    const char* raw =
        R"({"id":"page-2","text":"Bounty only","duration":2,"role":"bounty"})";
    EXPECT_EQ(pageManager.processPacket(nullptr, pageJson(raw, buf), buf.size()), 0);
    EXPECT_FALSE(pageManager.hasPendingPage());
}

TEST_F(PageManagerTestSuite, ingestMqttRelaysOpaqueBroadcast) {
    std::vector<uint8_t> buf;
    const char* raw = R"({"id":"page-3","text":"All hands","duration":5})";
    EXPECT_CALL(*device->mockPeerComms, sendData(_, PktType::kPageCommand, _, _)).Times(1);
    ASSERT_TRUE(pageManager.ingestMqttPayload(pageJson(raw, buf), buf.size()));
    ASSERT_TRUE(pageManager.hasPendingPage());
    EXPECT_EQ(pageManager.pendingPage().id, "page-3");
}
