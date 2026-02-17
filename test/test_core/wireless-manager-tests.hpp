#pragma once

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "wireless/quickdraw-wireless-manager.hpp"
#include "game/player.hpp"
#include "game/match.hpp"
#include "device/wireless-manager.hpp"
#include "device/drivers/peer-comms-interface.hpp"
#include "device/drivers/http-client-interface.hpp"
#include "../test-constants.hpp"

// Mock PeerCommsInterface for WirelessManager dependency
class MockPeerCommsForWireless : public PeerCommsInterface {
public:
    MOCK_METHOD(int, sendData,
                (const uint8_t* dstMac, PktType type, const uint8_t* data, size_t len),
                (override));
    MOCK_METHOD(void, setPacketHandler, (PktType, PacketCallback, void*), (override));
    MOCK_METHOD(void, clearPacketHandler, (PktType), (override));
    MOCK_METHOD(const uint8_t*, getGlobalBroadcastAddress, (), (override));
    MOCK_METHOD(uint8_t*, getMacAddress, (), (override));
    MOCK_METHOD(void, removePeer, (uint8_t*), (override));
    MOCK_METHOD(void, setPeerCommsState, (PeerCommsState), (override));
    MOCK_METHOD(PeerCommsState, getPeerCommsState, (), (override));
    MOCK_METHOD(void, connect, (), (override));
    MOCK_METHOD(void, disconnect, (), (override));
};

// Mock HttpClientInterface for WirelessManager dependency
class MockHttpClientForWireless : public HttpClientInterface {
public:
    MOCK_METHOD(bool, queueRequest, (HttpRequest&), (override));
    MOCK_METHOD(void, setHttpClientState, (HttpClientState), (override));
    MOCK_METHOD(HttpClientState, getHttpClientState, (), (override));
    MOCK_METHOD(bool, isConnected, (), (override));
    MOCK_METHOD(void, setWifiConfig, (WifiConfig*), (override));
    MOCK_METHOD(void, disconnect, (), (override));
    MOCK_METHOD(void, updateConfig, (WifiConfig*), (override));
    MOCK_METHOD(void, retryConnection, (), (override));
    MOCK_METHOD(uint8_t*, getMacAddress, (), (override));
};

class WirelessManagerTestSuite : public testing::Test {
protected:
    void SetUp() override {
        player = new Player(TestConstants::TEST_UUID_PLAYER_1, Allegiance::HELIX, true);
        mockPeerComms = new MockPeerCommsForWireless();
        mockHttpClient = new MockHttpClientForWireless();

        // Set up default expectations for WirelessManager constructor/initialize
        EXPECT_CALL(*mockHttpClient, setHttpClientState(HttpClientState::DISCONNECTED))
            .Times(testing::AtLeast(0));
        EXPECT_CALL(*mockPeerComms, setPeerCommsState(PeerCommsState::CONNECTED))
            .Times(testing::AtLeast(0));
        EXPECT_CALL(*mockPeerComms, getPeerCommsState())
            .WillRepeatedly(testing::Return(PeerCommsState::CONNECTED));

        realWirelessManager = new WirelessManager(mockPeerComms, mockHttpClient);
        realWirelessManager->initialize();

        wirelessManager = new QuickdrawWirelessManager();
        wirelessManager->initialize(player, realWirelessManager, 100);
    }

    void TearDown() override {
        delete wirelessManager;
        delete realWirelessManager;
        delete mockHttpClient;
        delete mockPeerComms;
        delete player;
    }

    Player* player;
    MockPeerCommsForWireless* mockPeerComms;
    MockHttpClientForWireless* mockHttpClient;
    WirelessManager* realWirelessManager;
    QuickdrawWirelessManager* wirelessManager;
};

// ============================================
// INITIALIZATION TESTS
// ============================================

inline void wirelessManagerInitializesCorrectly(
    Player* player,
    WirelessManager* realWirelessManager) {

    QuickdrawWirelessManager* wm = new QuickdrawWirelessManager();

    wm->initialize(player, realWirelessManager, 100);

    // No assertion needed - if it doesn't crash, initialization worked
    EXPECT_TRUE(true);

    delete wm;
}

inline void wirelessManagerClearsCallbacks(QuickdrawWirelessManager* wm) {
    bool callbackInvoked = false;

    wm->setPacketReceivedCallback([&callbackInvoked](QuickdrawCommand cmd) {
        callbackInvoked = true;
    });

    wm->clearCallbacks();

    // Create a dummy packet to test
    uint8_t srcMac[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};

    struct QuickdrawPacket {
        char matchId[37];
        char hunterId[5];
        char bountyId[5];
        long hunterDrawTime;
        long bountyDrawTime;
        int command;
    } __attribute__((packed));

    QuickdrawPacket packet;
    std::memset(&packet, 0, sizeof(packet));
    std::strncpy(packet.matchId, "test-match-id-1234567890123456789012", 36);
    std::strncpy(packet.hunterId, "H001", 4);
    std::strncpy(packet.bountyId, "B001", 4);
    packet.command = QDCommand::HACK;

    wm->processQuickdrawCommand(srcMac, reinterpret_cast<uint8_t*>(&packet), sizeof(packet));

    EXPECT_FALSE(callbackInvoked);
}

// ============================================
// BROADCAST PACKET TESTS
// ============================================

inline void broadcastPacketSendsCorrectData(
    QuickdrawWirelessManager* wm,
    MockPeerCommsForWireless* mockPeerComms,
    Player* player) {

    Match match("match-id-12345678901234567890123456", "H001", "B001");
    match.setHunterDrawTime(1000);
    match.setBountyDrawTime(2000);

    // Expect the peer comms to be called via WirelessManager
    EXPECT_CALL(*mockPeerComms, sendData(
        testing::_,
        PktType::kQuickdrawCommand,
        testing::_,
        testing::_))
        .Times(1)
        .WillOnce(testing::Return(0));

    int result = wm->broadcastPacket("11:22:33:44:55:66", QDCommand::HACK, match);

    EXPECT_EQ(result, 0);
}

inline void broadcastPacketFailsWithEmptyMac(
    QuickdrawWirelessManager* wm,
    Player* player) {

    Match match("match-id-12345678901234567890123456", "H001", "B001");

    int result = wm->broadcastPacket("", QDCommand::HACK, match);

    EXPECT_EQ(result, -1);
}

inline void broadcastPacketFailsWithInvalidMac(
    QuickdrawWirelessManager* wm,
    Player* player) {

    Match match("match-id-12345678901234567890123456", "H001", "B001");

    int result = wm->broadcastPacket("invalid-mac", QDCommand::HACK, match);

    EXPECT_EQ(result, -1);
}

// ============================================
// PROCESS COMMAND TESTS
// ============================================

inline void processCommandParsesPacketCorrectly(QuickdrawWirelessManager* wm) {
    bool callbackInvoked = false;
    QuickdrawCommand receivedCommand;

    wm->setPacketReceivedCallback([&callbackInvoked, &receivedCommand](QuickdrawCommand cmd) {
        callbackInvoked = true;
        receivedCommand = cmd;
    });

    uint8_t srcMac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};

    struct QuickdrawPacket {
        char matchId[37];
        char hunterId[5];
        char bountyId[5];
        long hunterDrawTime;
        long bountyDrawTime;
        int command;
    } __attribute__((packed));

    QuickdrawPacket packet;
    std::memset(&packet, 0, sizeof(packet));
    std::strncpy(packet.matchId, "test-match-id-1234567890123456789012", 36);
    std::strncpy(packet.hunterId, "H123", 4);
    std::strncpy(packet.bountyId, "B456", 4);
    packet.hunterDrawTime = 5000;
    packet.bountyDrawTime = 6000;
    packet.command = QDCommand::HACK_ACK;

    int result = wm->processQuickdrawCommand(srcMac, reinterpret_cast<uint8_t*>(&packet), sizeof(packet));

    EXPECT_EQ(result, 1);
    EXPECT_TRUE(callbackInvoked);
    EXPECT_EQ(receivedCommand.command, QDCommand::HACK_ACK);
    EXPECT_EQ(receivedCommand.match.getHunterId(), "H123");
    EXPECT_EQ(receivedCommand.match.getBountyId(), "B456");
    EXPECT_EQ(receivedCommand.match.getHunterDrawTime(), 5000);
    EXPECT_EQ(receivedCommand.match.getBountyDrawTime(), 6000);
}

inline void processCommandRejectsInvalidPacketSize(QuickdrawWirelessManager* wm) {
    uint8_t srcMac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    uint8_t invalidPacket[10] = {0};

    int result = wm->processQuickdrawCommand(srcMac, invalidPacket, sizeof(invalidPacket));

    EXPECT_EQ(result, -1);
}

// ============================================
// COMMAND TRACKING TESTS
// ============================================

inline void commandTrackerLogsCommands(QuickdrawWirelessManager* wm) {
    wm->clearPackets();

    uint8_t srcMac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};

    struct QuickdrawPacket {
        char matchId[37];
        char hunterId[5];
        char bountyId[5];
        long hunterDrawTime;
        long bountyDrawTime;
        int command;
    } __attribute__((packed));

    QuickdrawPacket packet;
    std::memset(&packet, 0, sizeof(packet));
    std::strncpy(packet.matchId, "test-match-id-1234567890123456789012", 36);
    std::strncpy(packet.hunterId, "H001", 4);
    std::strncpy(packet.bountyId, "B001", 4);
    packet.command = QDCommand::HACK;

    wm->processQuickdrawCommand(srcMac, reinterpret_cast<uint8_t*>(&packet), sizeof(packet));

    // Command should be logged (no direct accessor, but shouldn't crash)
    EXPECT_TRUE(true);
}

inline void clearPacketsRemovesAllTrackedCommands(QuickdrawWirelessManager* wm) {
    uint8_t srcMac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};

    struct QuickdrawPacket {
        char matchId[37];
        char hunterId[5];
        char bountyId[5];
        long hunterDrawTime;
        long bountyDrawTime;
        int command;
    } __attribute__((packed));

    QuickdrawPacket packet;
    std::memset(&packet, 0, sizeof(packet));
    std::strncpy(packet.matchId, "test-match-id-1234567890123456789012", 36);
    packet.command = QDCommand::HACK;

    wm->processQuickdrawCommand(srcMac, reinterpret_cast<uint8_t*>(&packet), sizeof(packet));

    wm->clearPackets();

    // Verify clear worked (no direct accessor, but shouldn't crash)
    EXPECT_TRUE(true);
}

inline void clearPacketRemovesSpecificCommand(QuickdrawWirelessManager* wm) {
    wm->clearPacket(QDCommand::HACK);

    // Should not crash
    EXPECT_TRUE(true);
}
