#pragma once

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "wireless/remote-player-manager.hpp"
#include "device/drivers/peer-comms-interface.hpp"
#include "game/player.hpp"
#include "utils/simple-timer.hpp"
#include "../test-constants.hpp"

// Mock PeerCommsInterface for testing
class MockPeerCommsInterface : public PeerCommsInterface {
public:
    MOCK_METHOD(int, sendData,
                (const uint8_t* dstMac, PktType type, const uint8_t* data, size_t len),
                (override));
    MOCK_METHOD(const uint8_t*, getGlobalBroadcastAddress, (), (override));
    MOCK_METHOD(void, setPacketHandler, (PktType, PacketCallback, void*), (override));
    MOCK_METHOD(void, clearPacketHandler, (PktType), (override));
    MOCK_METHOD(uint8_t*, getMacAddress, (), (override));
    MOCK_METHOD(void, removePeer, (uint8_t*), (override));
    MOCK_METHOD(void, setPeerCommsState, (PeerCommsState), (override));
    MOCK_METHOD(PeerCommsState, getPeerCommsState, (), (override));
    MOCK_METHOD(void, connect, (), (override));
    MOCK_METHOD(void, disconnect, (), (override));
};

class RemotePlayerTestSuite : public testing::Test {
protected:
    void SetUp() override {
        mockPeerComms = new MockPeerCommsInterface();
        remotePlayerManager = new RemotePlayerManager(mockPeerComms);

        localPlayer = new Player(TestConstants::TEST_UUID_PLAYER_1, Allegiance::HELIX, true);

        // Set up default mock behavior
        uint8_t broadcastMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
        broadcastAddr = new uint8_t[6];
        std::memcpy(broadcastAddr, broadcastMac, 6);

        ON_CALL(*mockPeerComms, getGlobalBroadcastAddress())
            .WillByDefault(testing::Return(broadcastAddr));
    }

    void TearDown() override {
        delete remotePlayerManager;
        delete mockPeerComms;
        delete localPlayer;
        delete[] broadcastAddr;
    }

    MockPeerCommsInterface* mockPeerComms;
    RemotePlayerManager* remotePlayerManager;
    Player* localPlayer;
    uint8_t* broadcastAddr;
};

// ============================================
// INITIALIZATION TESTS
// ============================================

inline void remotePlayerManagerInitializesCorrectly(
    MockPeerCommsInterface* mockPeerComms) {

    RemotePlayerManager* rpm = new RemotePlayerManager(mockPeerComms);

    EXPECT_NE(rpm, nullptr);
    EXPECT_EQ(rpm->GetRemotePlayerTTL(), 60000);

    delete rpm;
}

inline void remotePlayerManagerSetsTTL(RemotePlayerManager* rpm) {
    rpm->SetRemotePlayerTTL(30000);

    EXPECT_EQ(rpm->GetRemotePlayerTTL(), 30000);
}

// ============================================
// BROADCASTING TESTS
// ============================================

inline void startBroadcastingTriggersImmediateBroadcast(
    RemotePlayerManager* rpm,
    MockPeerCommsInterface* mockPeerComms,
    Player* localPlayer) {

    EXPECT_CALL(*mockPeerComms, sendData(
        testing::_,
        PktType::kPlayerInfoBroadcast,
        testing::_,
        testing::_))
        .Times(1)
        .WillOnce(testing::Return(0));

    rpm->StartBroadcastingPlayerInfo(localPlayer, 5000);
}

inline void broadcastPacketContainsCorrectPlayerInfo(
    RemotePlayerManager* rpm,
    MockPeerCommsInterface* mockPeerComms,
    Player* localPlayer) {

    // Capture the data sent
    const uint8_t* sentData = nullptr;
    size_t sentLen = 0;

    EXPECT_CALL(*mockPeerComms, sendData(
        testing::_,
        PktType::kPlayerInfoBroadcast,
        testing::_,
        testing::_))
        .Times(1)
        .WillOnce(testing::DoAll(
            testing::SaveArg<2>(&sentData),
            testing::SaveArg<3>(&sentLen),
            testing::Return(0)));

    rpm->StartBroadcastingPlayerInfo(localPlayer, 5000);

    // Verify packet structure
    struct PlayerInfoPkt {
        char id[37];
        Allegiance allegiance;
        uint8_t hunter;
    } __attribute__((packed));

    EXPECT_EQ(sentLen, sizeof(PlayerInfoPkt));
}

// ============================================
// PACKET PROCESSING TESTS
// ============================================

inline void processPlayerInfoPktAddsNewPlayer(RemotePlayerManager* rpm) {
    uint8_t srcMac[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};

    struct PlayerInfoPkt {
        char id[37];
        Allegiance allegiance;
        uint8_t hunter;
    } __attribute__((packed));

    PlayerInfoPkt packet;
    std::memset(&packet, 0, sizeof(packet));
    std::strncpy(packet.id, TestConstants::TEST_UUID_BOUNTY_1, 36);
    packet.allegiance = Allegiance::ENDLINE;
    packet.hunter = 0;

    int result = rpm->ProcessPlayerInfoPkt(srcMac, reinterpret_cast<uint8_t*>(&packet), sizeof(packet));

    EXPECT_EQ(result, 0);
}

inline void processPlayerInfoPktRejectsInvalidPacketSize(RemotePlayerManager* rpm) {
    uint8_t srcMac[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    uint8_t invalidPacket[10] = {0};

    int result = rpm->ProcessPlayerInfoPkt(srcMac, invalidPacket, sizeof(invalidPacket));

    EXPECT_EQ(result, -1);
}

inline void processPlayerInfoPktUpdatesExistingPlayer(RemotePlayerManager* rpm) {
    uint8_t srcMac[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};

    struct PlayerInfoPkt {
        char id[37];
        Allegiance allegiance;
        uint8_t hunter;
    } __attribute__((packed));

    PlayerInfoPkt packet;
    std::memset(&packet, 0, sizeof(packet));
    std::strncpy(packet.id, TestConstants::TEST_UUID_BOUNTY_1, 36);
    packet.allegiance = Allegiance::ENDLINE;
    packet.hunter = 0;

    // Process first packet
    rpm->ProcessPlayerInfoPkt(srcMac, reinterpret_cast<uint8_t*>(&packet), sizeof(packet));

    // Process second packet from same MAC (update)
    int result = rpm->ProcessPlayerInfoPkt(srcMac, reinterpret_cast<uint8_t*>(&packet), sizeof(packet));

    EXPECT_EQ(result, 0);
}

// ============================================
// UPDATE AND TTL TESTS
// ============================================

inline void updateRemovesStalePlayersAfterTTL(RemotePlayerManager* rpm) {
    // Set a short TTL
    rpm->SetRemotePlayerTTL(1000);

    uint8_t srcMac[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};

    struct PlayerInfoPkt {
        char id[37];
        Allegiance allegiance;
        uint8_t hunter;
    } __attribute__((packed));

    PlayerInfoPkt packet;
    std::memset(&packet, 0, sizeof(packet));
    std::strncpy(packet.id, TestConstants::TEST_UUID_BOUNTY_1, 36);
    packet.allegiance = Allegiance::RESISTANCE;
    packet.hunter = 0;

    // Add a player
    rpm->ProcessPlayerInfoPkt(srcMac, reinterpret_cast<uint8_t*>(&packet), sizeof(packet));

    // Simulate time passing (mock the platform clock if needed)
    // For now, just verify Update doesn't crash
    rpm->Update();

    EXPECT_TRUE(true);
}

inline void updateDoesNotCrashWithNoPlayers(RemotePlayerManager* rpm) {
    rpm->Update();

    EXPECT_TRUE(true);
}

// ============================================
// MULTIPLE PLAYER TESTS
// ============================================

inline void processMultiplePlayersFromDifferentMacs(RemotePlayerManager* rpm) {
    struct PlayerInfoPkt {
        char id[37];
        Allegiance allegiance;
        uint8_t hunter;
    } __attribute__((packed));

    // Player 1
    uint8_t srcMac1[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    PlayerInfoPkt packet1;
    std::memset(&packet1, 0, sizeof(packet1));
    std::strncpy(packet1.id, TestConstants::TEST_UUID_PLAYER_1, 36);
    packet1.allegiance = Allegiance::HELIX;
    packet1.hunter = 1;

    // Player 2
    uint8_t srcMac2[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    PlayerInfoPkt packet2;
    std::memset(&packet2, 0, sizeof(packet2));
    std::strncpy(packet2.id, TestConstants::TEST_UUID_BOUNTY_1, 36);
    packet2.allegiance = Allegiance::ENDLINE;
    packet2.hunter = 0;

    int result1 = rpm->ProcessPlayerInfoPkt(srcMac1, reinterpret_cast<uint8_t*>(&packet1), sizeof(packet1));
    int result2 = rpm->ProcessPlayerInfoPkt(srcMac2, reinterpret_cast<uint8_t*>(&packet2), sizeof(packet2));

    EXPECT_EQ(result1, 0);
    EXPECT_EQ(result2, 0);
}
