#pragma once

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <cstring>
#include <vector>

#include "device-mock.hpp"
#include "wireless/symbol-wireless-manager.hpp"

// The symbol exchange runs over a ReliableChannel the manager claims in its
// constructor. These pin the two halves that claim buys: frames leave as
// kSymbolMatchCommand, and an inbound frame is dispatched to the callback for
// the jack its sender sits on.
class SymbolWirelessTests : public testing::Test {
public:
    /// Stands up the manager on a mocked radio and captures its channel handler.
    void SetUp() override {
        fakeClock = new FakePlatformClock();
        SimpleTimer::setPlatformClock(fakeClock);

        ON_CALL(peerComms, getPeerCommsState())
            .WillByDefault(testing::Return(PeerCommsState::CONNECTED));
        ON_CALL(peerComms, sendData(testing::_, PktType::kSymbolMatchCommand,
                                    testing::_, testing::_))
            .WillByDefault(testing::Invoke(
                [this](const uint8_t* dst, PktType, const uint8_t* data, const size_t len) {
                    if (len == sizeof(SymbolMatchPacket)) {
                        SymbolMatchPacket packet{};
                        memcpy(&packet, data, sizeof(packet));
                        sent.push_back(packet);
                        memcpy(lastDestination, dst, 6);
                    }
                    return 1;
                }));
        // The channel installs its receive handler as it is constructed; holding
        // on to it is the only way to play the radio back at it.
        ON_CALL(peerComms, setPacketHandler(PktType::kSymbolMatchCommand,
                                            testing::_, testing::_))
            .WillByDefault(testing::Invoke(
                [this](PktType, PeerCommsInterface::PacketCallback callback, void* ctx) {
                    receiveHandler = callback;
                    receiveContext = ctx;
                }));

        wirelessManager = new WirelessManager(&peerComms, &httpClient);
        manager = new SymbolWirelessManager(wirelessManager, &rdc);
    }

    /// Frees the manager and the clock the timers hold.
    void TearDown() override {
        delete manager;
        delete wirelessManager;
        SimpleTimer::setPlatformClock(nullptr);
        delete fakeClock;
    }

    /// Plays one frame back at the manager as the radio would.
    void deliver(const uint8_t* fromMac, const SymbolMatchPacket& packet) {
        ASSERT_NE(receiveHandler, nullptr);
        receiveHandler(fromMac, reinterpret_cast<const uint8_t*>(&packet),
                       sizeof(packet), receiveContext);
    }

    testing::NiceMock<MockPeerComms> peerComms;
    testing::NiceMock<MockHttpClient> httpClient;
    FakeRemoteDeviceCoordinator rdc;
    WirelessManager* wirelessManager = nullptr;
    SymbolWirelessManager* manager = nullptr;
    FakePlatformClock* fakeClock = nullptr;

    std::vector<SymbolMatchPacket> sent;
    uint8_t lastDestination[6] = {};
    PeerCommsInterface::PacketCallback receiveHandler = nullptr;
    void* receiveContext = nullptr;
};

TEST_F(SymbolWirelessTests, sendPacketReachesRadioAsSymbolFrame) {
    const uint8_t peer[6] = {0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F};
    manager->setMacPeer(peer);

    manager->sendPacket(SMCommand::SEND_SYMBOL, SymbolId::SYMBOL_B,
                        SerialIdentifier::OUTPUT_JACK);

    ASSERT_EQ(sent.size(), 1u);
    EXPECT_EQ(sent[0].command, SMCommand::SEND_SYMBOL);
    EXPECT_EQ(sent[0].symbolId, SymbolId::SYMBOL_B);
    EXPECT_EQ(memcmp(lastDestination, peer, 6), 0);
    // Stamped, so the receiver can suppress a retransmit of this same frame.
    EXPECT_NE(sent[0].seqId, 0);
}

TEST_F(SymbolWirelessTests, receivedFrameDispatchesToItsJackCallback) {
    const uint8_t peer[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    rdc.setPeerMac(SerialIdentifier::OUTPUT_JACK, peer);

    int calls = 0;
    SymbolId seen = SymbolId::SYMBOL_A;
    manager->setPacketReceivedCallback(
        [&](const SymbolMatchCommand& command) {
            ++calls;
            seen = command.symbolId;
        },
        SerialIdentifier::OUTPUT_JACK);

    SymbolMatchPacket packet{};
    packet.seqId = 7;
    packet.command = SMCommand::SEND_SYMBOL;
    packet.symbolId = SymbolId::SYMBOL_C;
    deliver(peer, packet);

    EXPECT_EQ(calls, 1);
    EXPECT_EQ(seen, SymbolId::SYMBOL_C);
}

TEST_F(SymbolWirelessTests, frameFromAPeerOnNoJackIsDropped) {
    const uint8_t peer[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    const uint8_t stranger[6] = {0x99, 0x99, 0x99, 0x99, 0x99, 0x99};
    rdc.setPeerMac(SerialIdentifier::OUTPUT_JACK, peer);

    int calls = 0;
    manager->setPacketReceivedCallback(
        [&](const SymbolMatchCommand&) { ++calls; },
        SerialIdentifier::OUTPUT_JACK);

    SymbolMatchPacket packet{};
    packet.seqId = 3;
    packet.command = SMCommand::SEND_SYMBOL;
    packet.symbolId = SymbolId::SYMBOL_C;
    deliver(stranger, packet);

    EXPECT_EQ(calls, 0);
}

TEST_F(SymbolWirelessTests, retransmitOfTheSameFrameDispatchesOnce) {
    const uint8_t peer[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    rdc.setPeerMac(SerialIdentifier::OUTPUT_JACK, peer);

    int calls = 0;
    manager->setPacketReceivedCallback(
        [&](const SymbolMatchCommand&) { ++calls; },
        SerialIdentifier::OUTPUT_JACK);

    SymbolMatchPacket packet{};
    packet.seqId = 12;
    packet.command = SMCommand::SEND_SYMBOL;
    packet.symbolId = SymbolId::SYMBOL_A;
    deliver(peer, packet);
    deliver(peer, packet);

    EXPECT_EQ(calls, 1);
}
