#pragma once

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "device-mock.hpp"
#include "utility-tests.hpp"
#include "device/remote-device-coordinator.hpp"
#include "device/serial-frame-parser.hpp"
#include "device/drivers/native/native-serial-driver.hpp"
#include "protocol-constants.hpp"

#include <cstring>
#include <vector>

using ::testing::_;
using ::testing::Return;

// ============================================
// RDC per-jack HELLO connectivity (#155)
// ============================================
//
// Drives HELLO through the real production feed path: RDC wires the jack byte
// callback, bytes are pushed into the native driver's RX and drained by exec()
// into the SerialFrameParser. Single-threaded via setExternalConnectivityTask —
// the test calls emitHello()/sync() itself instead of the FreeRTOS task.

// Radio mock defaults shared by the fixture and the standalone ring nodes:
// sends and peer-table ops succeed, the device reports `mac` as its own.
inline void wireRadioDefaults(MockDevice& device, uint8_t* mac) {
    ON_CALL(*device.mockPeerComms, sendData(_, _, _, _)).WillByDefault(Return(1));
    ON_CALL(*device.mockPeerComms, getMacAddress()).WillByDefault(Return(mac));
    ON_CALL(*device.mockPeerComms, addEspNowPeer(_)).WillByDefault(Return(0));
    ON_CALL(*device.mockPeerComms, removeEspNowPeer(_)).WillByDefault(Return(0));
    ON_CALL(*device.mockPeerComms, getPeerCommsState())
        .WillByDefault(Return(PeerCommsState::CONNECTED));
}

// One direction of a cable: drains `from`'s emitted bytes into `to`'s RX and
// runs the real exec() pump.
inline void pumpCable(NativeSerialDriver& from, NativeSerialDriver& to) {
    const std::string bytes = from.getOutput();
    from.clearOutput();
    if (bytes.empty()) return;
    to.injectBytes(std::vector<uint8_t>(bytes.begin(), bytes.end()));
    to.exec();
}

// Pushes a frame into a jack's RX and drains it via the real exec() pump.
inline void deliverFrame(NativeSerialDriver& jack, const std::vector<uint8_t>& frame) {
    jack.injectBytes(frame);
    jack.exec();
}

class RDCHelloTests : public testing::Test {
public:
    /// Fake clock, mocked radio, native jacks swapped in, RDC in HELLO mode with
    /// the emit task externalized so the test drives it single-threaded.
    void SetUp() override {
        fakeClock = new FakePlatformClock();
        SimpleTimer::setPlatformClock(fakeClock);
        fakeClock->setTime(1000);

        wireRadioDefaults(device, localMac);

        device.serialManager->setOutputJack(&outJack);
        device.serialManager->setInputJack(&inJack);
        device.serialManager->setSecondaryInputJack(&secondaryJack);

        rdc.setExternalConnectivityTask(true);
        rdc.initialize(device.wirelessManager, device.serialManager, &device);
        rdc.setOnJackChange([this](SerialIdentifier jack, bool connected) {
            if (connected) {
                connectCount++;
                lastConnectJack = jack;
            } else {
                disconnectCount++;
                lastDisconnectJack = jack;
            }
        });
        rdc.enableHelloConnectivity();
    }

    /// Restores the real platform clock.
    void TearDown() override {
        SimpleTimer::setPlatformClock(nullptr);
        delete fakeClock;
    }

    /// A HELLO frame whose source MAC starts with firstByte (rest fixed).
    std::vector<uint8_t> helloFrame(uint8_t firstByte) {
        HelloPayload hello{};
        hello.source[0] = firstByte;
        hello.source[1] = 0x02;
        hello.source[2] = 0x03;
        hello.source[3] = 0x04;
        hello.source[4] = 0x05;
        hello.source[5] = 0x06;
        hello.deviceType = static_cast<uint8_t>(DeviceType::PDN);
        return encodeFramed(hello);
    }

    /// Pushes a frame into a jack's RX and drains it via the real exec() pump.
    void deliverHello(NativeSerialDriver& jack, const std::vector<uint8_t>& frame) {
        deliverFrame(jack, frame);
    }

    /// The RDC's owned transport, reached via friendship so a test can inject
    /// SEND_SUCCESS (onSendResult) and inbound contexts (deliverIncoming) without a
    /// radio. Not part of the production API.
    ReliableTransport* transport() { return rdc.transport; }

    MockDevice device;
    FakePlatformClock* fakeClock = nullptr;
    NativeSerialDriver outJack{"hello-out"};
    NativeSerialDriver inJack{"hello-in"};
    NativeSerialDriver secondaryJack{"hello-sec"};
    // Declared after the jacks so it is destroyed FIRST: the RDC dtor clears the
    // byte callbacks on the jacks, which must still be alive (mirrors production,
    // where the serial drivers outlive the RDC).
    RemoteDeviceCoordinator rdc;
    uint8_t localMac[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};

    int connectCount = 0;
    int disconnectCount = 0;
    SerialIdentifier lastConnectJack = SerialIdentifier::OUTPUT_JACK;
    SerialIdentifier lastDisconnectJack = SerialIdentifier::OUTPUT_JACK;
};

// A looped-back own HELLO or an all-zero-source (open-jack) HELLO must not open a
// link: on real hardware the output TX pin bleeds back through the TRS contacts,
// and treating that as a peer would keep a dead cable "alive". A genuine distinct
// peer must still connect, so the guard is not over-broad.
inline void rdcHelloRejectsSelfAndZeroSource(RDCHelloTests* suite) {
    HelloPayload zero{};
    zero.deviceType = static_cast<uint8_t>(DeviceType::PDN);
    suite->deliverHello(suite->outJack, encodeFramed(zero));
    EXPECT_EQ(suite->rdc.getHelloLinkState(SerialIdentifier::OUTPUT_JACK),
              RemoteDeviceCoordinator::HelloLinkState::IDLE);

    HelloPayload self{};
    for (int i = 0; i < 6; ++i) self.source[i] = suite->localMac[i];
    self.deviceType = static_cast<uint8_t>(DeviceType::PDN);
    suite->deliverHello(suite->outJack, encodeFramed(self));
    EXPECT_EQ(suite->rdc.getHelloLinkState(SerialIdentifier::OUTPUT_JACK),
              RemoteDeviceCoordinator::HelloLinkState::IDLE);

    suite->deliverHello(suite->outJack, suite->helloFrame(0xA1));
    suite->rdc.sync(&suite->device);
    EXPECT_EQ(suite->rdc.getHelloLinkState(SerialIdentifier::OUTPUT_JACK),
              RemoteDeviceCoordinator::HelloLinkState::CONNECTING);
}

// (a) A HELLO from a new MAC drives that jack Idle -> Connecting.
inline void rdcHelloNewMacDrivesConnecting(RDCHelloTests* suite) {
    EXPECT_EQ(suite->rdc.getHelloLinkState(SerialIdentifier::OUTPUT_JACK),
              RemoteDeviceCoordinator::HelloLinkState::IDLE);

    suite->deliverHello(suite->outJack, suite->helloFrame(0xA1));
    suite->rdc.sync(&suite->device);

    EXPECT_EQ(suite->rdc.getHelloLinkState(SerialIdentifier::OUTPUT_JACK),
              RemoteDeviceCoordinator::HelloLinkState::CONNECTING);
    EXPECT_EQ(suite->rdc.getPortStatus(SerialIdentifier::OUTPUT_JACK), PortStatus::CONNECTING);
    // A distinct jack is untouched.
    EXPECT_EQ(suite->rdc.getHelloLinkState(SerialIdentifier::INPUT_JACK),
              RemoteDeviceCoordinator::HelloLinkState::IDLE);
}

// (b) onContextExchangeComplete drives Connecting -> Connected and fires jack-connect.
inline void rdcHelloContextCompleteConnects(RDCHelloTests* suite) {
    suite->deliverHello(suite->outJack, suite->helloFrame(0xA1));
    suite->rdc.sync(&suite->device);
    ASSERT_EQ(suite->rdc.getHelloLinkState(SerialIdentifier::OUTPUT_JACK),
              RemoteDeviceCoordinator::HelloLinkState::CONNECTING);

    suite->rdc.onContextExchangeComplete(SerialIdentifier::OUTPUT_JACK);
    suite->rdc.sync(&suite->device);

    EXPECT_EQ(suite->rdc.getHelloLinkState(SerialIdentifier::OUTPUT_JACK),
              RemoteDeviceCoordinator::HelloLinkState::CONNECTED);
    EXPECT_EQ(suite->rdc.getPortStatus(SerialIdentifier::OUTPUT_JACK), PortStatus::CONNECTED);
    EXPECT_EQ(suite->connectCount, 1);
    EXPECT_EQ(suite->lastConnectJack, SerialIdentifier::OUTPUT_JACK);
    EXPECT_EQ(suite->disconnectCount, 0);
}

// (c) Clock past silent-link with no HELLO drives Connected -> Idle, fires disconnect.
inline void rdcHelloSilentLinkDisconnects(RDCHelloTests* suite) {
    suite->deliverHello(suite->outJack, suite->helloFrame(0xA1));
    suite->rdc.sync(&suite->device);
    suite->rdc.onContextExchangeComplete(SerialIdentifier::OUTPUT_JACK);
    suite->rdc.sync(&suite->device);
    ASSERT_EQ(suite->rdc.getHelloLinkState(SerialIdentifier::OUTPUT_JACK),
              RemoteDeviceCoordinator::HelloLinkState::CONNECTED);

    suite->fakeClock->advance(RemoteDeviceCoordinator::HELLO_SILENT_LINK_MS + 1);
    suite->rdc.sync(&suite->device);

    EXPECT_EQ(suite->rdc.getHelloLinkState(SerialIdentifier::OUTPUT_JACK),
              RemoteDeviceCoordinator::HelloLinkState::IDLE);
    EXPECT_EQ(suite->rdc.getPortStatus(SerialIdentifier::OUTPUT_JACK), PortStatus::DISCONNECTED);
    EXPECT_EQ(suite->disconnectCount, 1);
    EXPECT_EQ(suite->lastDisconnectJack, SerialIdentifier::OUTPUT_JACK);
}

// (d) The emit step produces HELLO frame bytes on both jacks, carrying our MAC.
inline void rdcHelloEmitProducesFramesOnBothJacks(RDCHelloTests* suite) {
    suite->outJack.clearOutput();
    suite->inJack.clearOutput();

    suite->rdc.emitHello();

    for (const std::string& out : {suite->outJack.getOutput(), suite->inJack.getOutput()}) {
        ASSERT_GE(out.size(), static_cast<size_t>(HELLO_PAYLOAD_LEN));
        EXPECT_EQ(static_cast<uint8_t>(out[0]), FRAME_PREAMBLE_0);
        EXPECT_EQ(static_cast<uint8_t>(out[1]), FRAME_PREAMBLE_1);

        // Decode the emitted bytes back through the parser: it must be a valid
        // HELLO carrying this device's MAC and type.
        SerialFrameParser parser;
        int frames = 0;
        HelloPayload got{};
        parser.setHelloFrameHandler([&](const HelloPayload& h) {
            got = h;
            frames++;
        });
        std::vector<uint8_t> bytes(out.begin(), out.end());
        parser.feed(bytes.data(), bytes.size());

        ASSERT_EQ(frames, 1);
        EXPECT_EQ(0, memcmp(got.source, suite->localMac, 6));
        EXPECT_EQ(got.deviceType, static_cast<uint8_t>(DeviceType::PDN));
    }
}

// (e) A new HELLO MAC on EITHER jack sends this device's context (pending entry
// created): every jack initiates its own context on connecting.
inline void rdcHelloEveryJackInitiatesContext(RDCHelloTests* suite) {
    const uint8_t outPeer[6] = {0xA1, 0x02, 0x03, 0x04, 0x05, 0x06};
    const uint8_t inPeer[6] = {0xB1, 0x02, 0x03, 0x04, 0x05, 0x06};

    suite->deliverHello(suite->outJack, suite->helloFrame(0xA1));
    suite->deliverHello(suite->inJack, suite->helloFrame(0xB1));
    suite->rdc.sync(&suite->device);

    EXPECT_TRUE(suite->rdc.isContextSendPending(outPeer));
    EXPECT_TRUE(suite->rdc.isContextSendPending(inPeer));
    EXPECT_EQ(suite->rdc.getHelloLinkState(SerialIdentifier::INPUT_JACK),
              RemoteDeviceCoordinator::HelloLinkState::CONNECTING);
}

// A PdnConnectionContext carrying the given chainRole + userId, serialized to its
// wire bytes for deliverIncoming.
inline std::vector<uint8_t> pdnContextBytes(uint8_t chainRole, uint16_t userId, uint8_t seqId) {
    PdnConnectionContext ctx{};
    ctx.seqId = seqId;
    ctx.chainRole = chainRole;
    ctx.player.userId = userId;
    std::vector<uint8_t> bytes(sizeof(ctx));
    memcpy(bytes.data(), &ctx, sizeof(ctx));
    return bytes;
}

// (a)+(b) A new OUT-jack HELLO MAC sends context (pending), and the #152
// SEND_SUCCESS onSendResult clears that pending entry.
inline void rdcContextSendClearedBySendSuccess(RDCHelloTests* suite) {
    const uint8_t peer[6] = {0xA1, 0x02, 0x03, 0x04, 0x05, 0x06};

    // Capture the exact bytes handed to the radio so the echoed seqId can be
    // fed back through onSendResult, exactly as the driver's send callback does.
    std::vector<uint8_t> sentPayload;
    ON_CALL(*suite->device.mockPeerComms,
            sendData(_, PktType::kPdnConnectionContext, _, _))
        .WillByDefault(testing::DoAll(
            testing::Invoke([&sentPayload](const uint8_t*, PktType, const uint8_t* data,
                                           size_t len) {
                sentPayload.assign(data, data + len);
            }),
            Return(1)));

    suite->deliverHello(suite->outJack, suite->helloFrame(0xA1));
    suite->rdc.sync(&suite->device);  // drive Idle->Connecting; fires onContextRequest
    ASSERT_TRUE(suite->rdc.isContextSendPending(peer));
    ASSERT_EQ(sentPayload.size(), sizeof(PdnConnectionContext));

    suite->transport()->onSendResult(
        PktType::kPdnConnectionContext, peer, sentPayload.data(), sentPayload.size(), true);

    EXPECT_FALSE(suite->rdc.isContextSendPending(peer));
}

// (c) Receiving a peer's context registers it, records chainRole, fires the
// opaque profile callback with the matched jack, and drives that jack
// CONNECTING -> CONNECTED.
inline void rdcContextReceiveConnectsJack(RDCHelloTests* suite) {
    const uint8_t peer[6] = {0xA1, 0x02, 0x03, 0x04, 0x05, 0x06};

    SerialIdentifier cbJack = SerialIdentifier::INPUT_JACK;
    DeviceType cbType = DeviceType::UNKNOWN;
    uint16_t cbUserId = 0;
    int cbCount = 0;
    suite->rdc.setOnContextReceived(
        [&](SerialIdentifier jack, DeviceType type, const uint8_t* profile, size_t len) {
            cbCount++;
            cbJack = jack;
            cbType = type;
            ASSERT_EQ(len, sizeof(PlayerProfile));
            PlayerProfile p{};
            memcpy(&p, profile, len);
            cbUserId = p.userId;
        });

    // The OUT jack must already know the peer MAC (from its HELLO) for the
    // received context to match a jack.
    suite->deliverHello(suite->outJack, suite->helloFrame(0xA1));
    suite->rdc.sync(&suite->device);  // drive Idle->Connecting; fires onContextRequest
    ASSERT_EQ(suite->rdc.getHelloLinkState(SerialIdentifier::OUTPUT_JACK),
              RemoteDeviceCoordinator::HelloLinkState::CONNECTING);

    EXPECT_CALL(*suite->device.mockPeerComms, addEspNowPeer(_))
        .Times(testing::AnyNumber());

    std::vector<uint8_t> ctx = pdnContextBytes(/*chainRole=*/2, /*userId=*/4242, /*seqId=*/9);
    suite->transport()->deliverIncoming(
        PktType::kPdnConnectionContext, peer, ctx.data(), ctx.size());
    suite->rdc.sync(&suite->device);  // commit Connecting->Connected

    EXPECT_EQ(cbCount, 1);
    EXPECT_EQ(cbJack, SerialIdentifier::OUTPUT_JACK);
    EXPECT_EQ(cbType, DeviceType::PDN);
    EXPECT_EQ(cbUserId, 4242);
    EXPECT_EQ(suite->rdc.getPeerChainRole(SerialIdentifier::OUTPUT_JACK), 2);
    EXPECT_EQ(suite->rdc.getHelloLinkState(SerialIdentifier::OUTPUT_JACK),
              RemoteDeviceCoordinator::HelloLinkState::CONNECTED);
    EXPECT_EQ(suite->connectCount, 1);
    EXPECT_EQ(suite->lastConnectJack, SerialIdentifier::OUTPUT_JACK);
}

// (f) Every jack initiates: a HELLO on the INPUT jack sends our context just like
// the OUT jack does, and receiving the peer's context completes that jack. No
// reply step, so two devices whose jacks face each other can't ping-pong.
inline void rdcContextInputJackInitiates(RDCHelloTests* suite) {
    const uint8_t peer[6] = {0xB1, 0x02, 0x03, 0x04, 0x05, 0x06};

    ON_CALL(*suite->device.mockPeerComms, sendData(_, _, _, _))
        .WillByDefault(Return(1));
    EXPECT_CALL(*suite->device.mockPeerComms, addEspNowPeer(_))
        .Times(testing::AnyNumber());

    // Peer arrives on the INPUT jack; the jack initiates its own context on connecting.
    suite->deliverHello(suite->inJack, suite->helloFrame(0xB1));
    suite->rdc.sync(&suite->device);  // drive Idle->Connecting; input jack initiates
    ASSERT_EQ(suite->rdc.getHelloLinkState(SerialIdentifier::INPUT_JACK),
              RemoteDeviceCoordinator::HelloLinkState::CONNECTING);
    ASSERT_TRUE(suite->rdc.isContextSendPending(peer));

    // Receiving the peer's context completes the input jack; nothing is sent back.
    std::vector<uint8_t> ctx = pdnContextBytes(/*chainRole=*/1, /*userId=*/7, /*seqId=*/3);
    suite->transport()->deliverIncoming(
        PktType::kPdnConnectionContext, peer, ctx.data(), ctx.size());
    suite->rdc.sync(&suite->device);  // commit Connecting->Connected

    EXPECT_EQ(suite->rdc.getHelloLinkState(SerialIdentifier::INPUT_JACK),
              RemoteDeviceCoordinator::HelloLinkState::CONNECTED);
}

// A 2-node ring cables the same peer to BOTH jacks. One received context must
// complete BOTH links; if the un-matched one is left CONNECTING it silent-links to
// IDLE and opens the ring.
inline void rdcContextCompletesBothJacksForSamePeer(RDCHelloTests* suite) {
    const uint8_t peer[6] = {0xB1, 0x02, 0x03, 0x04, 0x05, 0x06};

    ON_CALL(*suite->device.mockPeerComms, sendData(_, _, _, _)).WillByDefault(Return(1));
    EXPECT_CALL(*suite->device.mockPeerComms, addEspNowPeer(_)).Times(testing::AnyNumber());
    int contextSends = 0;
    ON_CALL(*suite->device.mockPeerComms, sendData(_, PktType::kPdnConnectionContext, _, _))
        .WillByDefault(testing::DoAll(
            testing::InvokeWithoutArgs([&contextSends]() { contextSends++; }), Return(1)));

    suite->deliverHello(suite->outJack, suite->helloFrame(0xB1));
    suite->deliverHello(suite->inJack, suite->helloFrame(0xB1));
    suite->rdc.sync(&suite->device);
    ASSERT_EQ(suite->rdc.getHelloLinkState(SerialIdentifier::OUTPUT_JACK),
              RemoteDeviceCoordinator::HelloLinkState::CONNECTING);
    ASSERT_EQ(suite->rdc.getHelloLinkState(SerialIdentifier::INPUT_JACK),
              RemoteDeviceCoordinator::HelloLinkState::CONNECTING);
    // Both jacks face the same peer, but our context goes out ONCE, not twice.
    EXPECT_EQ(contextSends, 1);

    std::vector<uint8_t> ctx = pdnContextBytes(/*chainRole=*/1, /*userId=*/7, /*seqId=*/3);
    suite->transport()->deliverIncoming(
        PktType::kPdnConnectionContext, peer, ctx.data(), ctx.size());
    suite->rdc.sync(&suite->device);

    EXPECT_EQ(suite->rdc.getHelloLinkState(SerialIdentifier::OUTPUT_JACK),
              RemoteDeviceCoordinator::HelloLinkState::CONNECTED);
    EXPECT_EQ(suite->rdc.getHelloLinkState(SerialIdentifier::INPUT_JACK),
              RemoteDeviceCoordinator::HelloLinkState::CONNECTED);
}

// A context that beats its own HELLO (arrives while the jack is still IDLE) must be
// buffered and applied when the jack connects, not dropped — else the link half-opens.
inline void rdcContextBeforeConnectingIsBufferedAndApplied(RDCHelloTests* suite) {
    const uint8_t peer[6] = {0xB1, 0x02, 0x03, 0x04, 0x05, 0x06};

    ON_CALL(*suite->device.mockPeerComms, sendData(_, _, _, _)).WillByDefault(Return(1));
    EXPECT_CALL(*suite->device.mockPeerComms, addEspNowPeer(_)).Times(testing::AnyNumber());

    // Context arrives BEFORE any HELLO: the input jack is still Idle, so it can't be
    // completed yet — but it must be held, not discarded.
    ASSERT_EQ(suite->rdc.getHelloLinkState(SerialIdentifier::INPUT_JACK),
              RemoteDeviceCoordinator::HelloLinkState::IDLE);
    std::vector<uint8_t> ctx = pdnContextBytes(/*chainRole=*/1, /*userId=*/7, /*seqId=*/3);
    suite->transport()->deliverIncoming(
        PktType::kPdnConnectionContext, peer, ctx.data(), ctx.size());
    suite->rdc.sync(&suite->device);
    ASSERT_EQ(suite->rdc.getHelloLinkState(SerialIdentifier::INPUT_JACK),
              RemoteDeviceCoordinator::HelloLinkState::IDLE);

    // The HELLO finally arrives: the jack connects and the buffered context completes
    // it (no re-send from the peer needed).
    suite->deliverHello(suite->inJack, suite->helloFrame(0xB1));
    suite->rdc.sync(&suite->device);  // Idle->Connecting drains the buffer
    suite->rdc.sync(&suite->device);  // commit Connecting->Connected

    EXPECT_EQ(suite->rdc.getHelloLinkState(SerialIdentifier::INPUT_JACK),
              RemoteDeviceCoordinator::HelloLinkState::CONNECTED);
}

// A 2-node ring (same peer on BOTH jacks) whose context beats both HELLOs: it is
// cached while both jacks are still IDLE, then the jacks enter CONNECTING one at a
// time (sync drives OUTPUT before INPUT). The first to connect must NOT consume the
// cache out from under the second — both links must reach CONNECTED, and each jack's
// context callback must fire exactly once. Regression: a per-arrival consume left the
// second jack CONNECTING until it silent-linked to IDLE, half-opening the ring.
inline void rdcCachedContextCompletesBoth2NodeRingJacks(RDCHelloTests* suite) {
    const uint8_t peer[6] = {0xB1, 0x02, 0x03, 0x04, 0x05, 0x06};

    ON_CALL(*suite->device.mockPeerComms, sendData(_, _, _, _)).WillByDefault(Return(1));
    EXPECT_CALL(*suite->device.mockPeerComms, addEspNowPeer(_)).Times(testing::AnyNumber());

    int outCb = 0;
    int inCb = 0;
    suite->rdc.setOnContextReceived(
        [&](SerialIdentifier jack, DeviceType, const uint8_t*, size_t) {
            if (jack == SerialIdentifier::OUTPUT_JACK) outCb++;
            else if (jack == SerialIdentifier::INPUT_JACK) inCb++;
        });

    // Context arrives before EITHER jack is CONNECTING: both are Idle, so it is cached.
    ASSERT_EQ(suite->rdc.getHelloLinkState(SerialIdentifier::OUTPUT_JACK),
              RemoteDeviceCoordinator::HelloLinkState::IDLE);
    ASSERT_EQ(suite->rdc.getHelloLinkState(SerialIdentifier::INPUT_JACK),
              RemoteDeviceCoordinator::HelloLinkState::IDLE);
    std::vector<uint8_t> ctx = pdnContextBytes(/*chainRole=*/1, /*userId=*/7, /*seqId=*/3);
    suite->transport()->deliverIncoming(
        PktType::kPdnConnectionContext, peer, ctx.data(), ctx.size());

    // Both HELLOs arrive from the same peer; one sync drives both jacks Idle->Connecting
    // (OUTPUT first). OUTPUT drains the cache, and INPUT — connecting right after — must
    // still find it.
    suite->deliverHello(suite->outJack, suite->helloFrame(0xB1));
    suite->deliverHello(suite->inJack, suite->helloFrame(0xB1));
    suite->rdc.sync(&suite->device);  // both drain the cache as they connect
    suite->rdc.sync(&suite->device);  // commit Connecting->Connected

    EXPECT_EQ(suite->rdc.getHelloLinkState(SerialIdentifier::OUTPUT_JACK),
              RemoteDeviceCoordinator::HelloLinkState::CONNECTED);
    EXPECT_EQ(suite->rdc.getHelloLinkState(SerialIdentifier::INPUT_JACK),
              RemoteDeviceCoordinator::HelloLinkState::CONNECTED);
    EXPECT_EQ(outCb, 1);
    EXPECT_EQ(inCb, 1);
}

// Every link death must release the peer's ESP-NOW slot (the radio hard-fails past
// its peer cap, so leaked slots eventually block all new connections): both the
// Connected silent-link edge and the Connecting context-timeout edge.
inline void rdcLinkDeathReleasesPeerSlot(RDCHelloTests* suite) {
    const uint8_t peer[6] = {0xA1, 0x02, 0x03, 0x04, 0x05, 0x06};
    const uint8_t secondPeer[6] = {0xC1, 0x02, 0x03, 0x04, 0x05, 0x06};

    int removed = 0;
    std::array<uint8_t, 6> removedMac{};
    EXPECT_CALL(*suite->device.mockPeerComms, addEspNowPeer(_)).Times(testing::AnyNumber());
    EXPECT_CALL(*suite->device.mockPeerComms, removeEspNowPeer(_))
        .WillRepeatedly(testing::DoAll(
            testing::Invoke([&](const uint8_t* mac) {
                removed++;
                memcpy(removedMac.data(), mac, 6);
            }),
            Return(0)));

    // Connected -> Idle (silent link) releases the slot.
    suite->deliverHello(suite->outJack, suite->helloFrame(0xA1));
    suite->rdc.sync(&suite->device);
    suite->rdc.onContextExchangeComplete(SerialIdentifier::OUTPUT_JACK);
    suite->rdc.sync(&suite->device);
    ASSERT_EQ(suite->rdc.getHelloLinkState(SerialIdentifier::OUTPUT_JACK),
              RemoteDeviceCoordinator::HelloLinkState::CONNECTED);
    EXPECT_EQ(removed, 0);

    suite->fakeClock->advance(RemoteDeviceCoordinator::HELLO_SILENT_LINK_MS + 1);
    suite->rdc.sync(&suite->device);
    EXPECT_EQ(removed, 1);
    EXPECT_EQ(0, memcmp(removedMac.data(), peer, 6));

    // Connecting -> Idle (context exchange never completes) releases too.
    suite->deliverHello(suite->outJack, suite->helloFrame(0xC1));
    suite->rdc.sync(&suite->device);
    ASSERT_EQ(suite->rdc.getHelloLinkState(SerialIdentifier::OUTPUT_JACK),
              RemoteDeviceCoordinator::HelloLinkState::CONNECTING);

    suite->fakeClock->advance(RemoteDeviceCoordinator::CONTEXT_EXCHANGE_TIMEOUT_MS + 1);
    suite->rdc.sync(&suite->device);
    EXPECT_EQ(removed, 2);
    EXPECT_EQ(0, memcmp(removedMac.data(), secondPeer, 6));
}

// A 2-node ring has the same MAC on both jacks: dropping ONE cable must NOT release
// the ESP-NOW slot the still-connected jack depends on; dropping the second one must.
inline void rdc2NodeRingSingleJackDropKeepsPeerSlot(RDCHelloTests* suite) {
    const uint8_t peer[6] = {0xB1, 0x02, 0x03, 0x04, 0x05, 0x06};

    int removed = 0;
    EXPECT_CALL(*suite->device.mockPeerComms, addEspNowPeer(_)).Times(testing::AnyNumber());
    EXPECT_CALL(*suite->device.mockPeerComms, removeEspNowPeer(_))
        .WillRepeatedly(testing::DoAll(
            testing::InvokeWithoutArgs([&removed]() { removed++; }), Return(0)));

    suite->deliverHello(suite->outJack, suite->helloFrame(0xB1));
    suite->deliverHello(suite->inJack, suite->helloFrame(0xB1));
    suite->rdc.sync(&suite->device);
    std::vector<uint8_t> ctx = pdnContextBytes(/*chainRole=*/1, /*userId=*/7, /*seqId=*/3);
    suite->transport()->deliverIncoming(
        PktType::kPdnConnectionContext, peer, ctx.data(), ctx.size());
    suite->rdc.sync(&suite->device);
    ASSERT_EQ(suite->rdc.getHelloLinkState(SerialIdentifier::OUTPUT_JACK),
              RemoteDeviceCoordinator::HelloLinkState::CONNECTED);
    ASSERT_EQ(suite->rdc.getHelloLinkState(SerialIdentifier::INPUT_JACK),
              RemoteDeviceCoordinator::HelloLinkState::CONNECTED);

    // One cable dies: only the INPUT jack keeps hearing HELLOs across the gap.
    suite->fakeClock->advance(RemoteDeviceCoordinator::HELLO_SILENT_LINK_MS + 1);
    suite->deliverHello(suite->inJack, suite->helloFrame(0xB1));
    suite->rdc.sync(&suite->device);
    ASSERT_EQ(suite->rdc.getHelloLinkState(SerialIdentifier::OUTPUT_JACK),
              RemoteDeviceCoordinator::HelloLinkState::IDLE);
    ASSERT_EQ(suite->rdc.getHelloLinkState(SerialIdentifier::INPUT_JACK),
              RemoteDeviceCoordinator::HelloLinkState::CONNECTED);
    EXPECT_EQ(removed, 0);  // the surviving link still needs the radio slot

    // The second cable dies too: now the slot is released.
    suite->fakeClock->advance(RemoteDeviceCoordinator::HELLO_SILENT_LINK_MS + 1);
    suite->rdc.sync(&suite->device);
    ASSERT_EQ(suite->rdc.getHelloLinkState(SerialIdentifier::INPUT_JACK),
              RemoteDeviceCoordinator::HelloLinkState::IDLE);
    EXPECT_EQ(removed, 1);
}

// Link death must cancel the pending context send along with the radio slot: a
// surviving retry re-registers its target inside the driver, re-adding (and thus
// permanently leaking) the slot that was just released.
inline void rdcLinkDeathCancelsPendingContextSend(RDCHelloTests* suite) {
    const uint8_t peer[6] = {0xA1, 0x02, 0x03, 0x04, 0x05, 0x06};

    EXPECT_CALL(*suite->device.mockPeerComms, addEspNowPeer(_)).Times(testing::AnyNumber());
    int contextSends = 0;
    ON_CALL(*suite->device.mockPeerComms, sendData(_, PktType::kPdnConnectionContext, _, _))
        .WillByDefault(testing::DoAll(
            testing::InvokeWithoutArgs([&contextSends]() { contextSends++; }), Return(1)));

    // Send goes out but is never acked, so it sits pending on the Resender.
    suite->deliverHello(suite->outJack, suite->helloFrame(0xA1));
    suite->rdc.sync(&suite->device);
    ASSERT_TRUE(suite->rdc.isContextSendPending(peer));

    // The exchange never completes; the link dies.
    suite->fakeClock->advance(RemoteDeviceCoordinator::CONTEXT_EXCHANGE_TIMEOUT_MS + 1);
    suite->rdc.sync(&suite->device);
    ASSERT_EQ(suite->rdc.getHelloLinkState(SerialIdentifier::OUTPUT_JACK),
              RemoteDeviceCoordinator::HelloLinkState::IDLE);
    EXPECT_FALSE(suite->rdc.isContextSendPending(peer));

    // Pumping the retransmit timers after death must produce no further sends.
    const int sendsAtDeath = contextSends;
    for (int i = 0; i < 4; ++i) {
        suite->fakeClock->advance(400);
        suite->rdc.sync(&suite->device);
    }
    EXPECT_EQ(contextSends, sendsAtDeath);
}

// A replug right after a failed exchange must run a fresh full exchange: register
// the slot again and send our context again (the dead attempt's pending entry must
// not short-circuit the new one), ending CONNECTED.
inline void rdcReplugAfterFailedExchangeRecovers(RDCHelloTests* suite) {
    const uint8_t peer[6] = {0xA1, 0x02, 0x03, 0x04, 0x05, 0x06};

    int adds = 0;
    int removes = 0;
    EXPECT_CALL(*suite->device.mockPeerComms, addEspNowPeer(_))
        .WillRepeatedly(testing::DoAll(
            testing::InvokeWithoutArgs([&adds]() { adds++; }), Return(0)));
    EXPECT_CALL(*suite->device.mockPeerComms, removeEspNowPeer(_))
        .WillRepeatedly(testing::DoAll(
            testing::InvokeWithoutArgs([&removes]() { removes++; }), Return(0)));
    int contextSends = 0;
    ON_CALL(*suite->device.mockPeerComms, sendData(_, PktType::kPdnConnectionContext, _, _))
        .WillByDefault(testing::DoAll(
            testing::InvokeWithoutArgs([&contextSends]() { contextSends++; }), Return(1)));

    // First exchange: send never acked, link dies with the send still un-acked.
    suite->deliverHello(suite->outJack, suite->helloFrame(0xA1));
    suite->rdc.sync(&suite->device);
    suite->fakeClock->advance(RemoteDeviceCoordinator::CONTEXT_EXCHANGE_TIMEOUT_MS + 1);
    suite->rdc.sync(&suite->device);
    ASSERT_EQ(suite->rdc.getHelloLinkState(SerialIdentifier::OUTPUT_JACK),
              RemoteDeviceCoordinator::HelloLinkState::IDLE);
    ASSERT_EQ(removes, 1);
    const int sendsBeforeReplug = contextSends;
    const int addsBeforeReplug = adds;

    // Replug: the fresh exchange must re-register and re-send, then complete.
    suite->deliverHello(suite->outJack, suite->helloFrame(0xA1));
    suite->rdc.sync(&suite->device);
    ASSERT_EQ(suite->rdc.getHelloLinkState(SerialIdentifier::OUTPUT_JACK),
              RemoteDeviceCoordinator::HelloLinkState::CONNECTING);
    EXPECT_EQ(adds, addsBeforeReplug + 1);
    EXPECT_EQ(contextSends, sendsBeforeReplug + 1);

    std::vector<uint8_t> ctx = pdnContextBytes(/*chainRole=*/1, /*userId=*/7, /*seqId=*/3);
    suite->transport()->deliverIncoming(
        PktType::kPdnConnectionContext, peer, ctx.data(), ctx.size());
    suite->rdc.sync(&suite->device);
    EXPECT_EQ(suite->rdc.getHelloLinkState(SerialIdentifier::OUTPUT_JACK),
              RemoteDeviceCoordinator::HelloLinkState::CONNECTED);
    EXPECT_EQ(removes, 1);
}

// Half-open recovery: we are CONNECTED but the peer never got our context (lost
// app-side, or it rebooted). Its retry arrives from a MAC our CONNECTED jack already
// tracks; we must resend our context so its cycle can complete, and stay CONNECTED.
inline void rdcConnectedPeerRetryTriggersContextResend(RDCHelloTests* suite) {
    const uint8_t peer[6] = {0xA1, 0x02, 0x03, 0x04, 0x05, 0x06};
    const uint8_t stranger[6] = {0xD1, 0x02, 0x03, 0x04, 0x05, 0x06};

    EXPECT_CALL(*suite->device.mockPeerComms, addEspNowPeer(_)).Times(testing::AnyNumber());
    int contextSends = 0;
    std::vector<uint8_t> sentPayload;
    ON_CALL(*suite->device.mockPeerComms, sendData(_, PktType::kPdnConnectionContext, _, _))
        .WillByDefault(testing::DoAll(
            testing::Invoke([&](const uint8_t*, PktType, const uint8_t* data, size_t len) {
                contextSends++;
                sentPayload.assign(data, data + len);
            }),
            Return(1)));

    // Full connect on the OUT jack; SEND_SUCCESS clears our pending send.
    suite->deliverHello(suite->outJack, suite->helloFrame(0xA1));
    suite->rdc.sync(&suite->device);
    ASSERT_EQ(contextSends, 1);
    suite->transport()->onSendResult(
        PktType::kPdnConnectionContext, peer, sentPayload.data(), sentPayload.size(), true);
    ASSERT_FALSE(suite->rdc.isContextSendPending(peer));

    std::vector<uint8_t> ctx = pdnContextBytes(/*chainRole=*/1, /*userId=*/7, /*seqId=*/3);
    suite->transport()->deliverIncoming(
        PktType::kPdnConnectionContext, peer, ctx.data(), ctx.size());
    suite->rdc.sync(&suite->device);
    ASSERT_EQ(suite->rdc.getHelloLinkState(SerialIdentifier::OUTPUT_JACK),
              RemoteDeviceCoordinator::HelloLinkState::CONNECTED);
    ASSERT_EQ(contextSends, 1);

    // The peer retries (fresh seqId): we resend our context and stay CONNECTED.
    std::vector<uint8_t> retry = pdnContextBytes(/*chainRole=*/1, /*userId=*/7, /*seqId=*/4);
    suite->transport()->deliverIncoming(
        PktType::kPdnConnectionContext, peer, retry.data(), retry.size());
    suite->rdc.sync(&suite->device);
    EXPECT_EQ(contextSends, 2);
    EXPECT_TRUE(suite->rdc.isContextSendPending(peer));
    EXPECT_EQ(suite->rdc.getHelloLinkState(SerialIdentifier::OUTPUT_JACK),
              RemoteDeviceCoordinator::HelloLinkState::CONNECTED);

    // A context from a MAC no jack tracks is buffered only — no resend to strangers.
    std::vector<uint8_t> unknown = pdnContextBytes(/*chainRole=*/1, /*userId=*/9, /*seqId=*/5);
    suite->transport()->deliverIncoming(
        PktType::kPdnConnectionContext, stranger, unknown.data(), unknown.size());
    EXPECT_EQ(contextSends, 2);
}

// Two CONNECTED sides can answer each other's recovery resends forever (every send
// stamps a fresh seqId, so dedup never brakes), so the resend is throttled to one
// per exchange window, per jack.
inline void rdcConnectedRetryResendThrottled(RDCHelloTests* suite) {
    const uint8_t peer[6] = {0xA1, 0x02, 0x03, 0x04, 0x05, 0x06};

    EXPECT_CALL(*suite->device.mockPeerComms, addEspNowPeer(_)).Times(testing::AnyNumber());
    int contextSends = 0;
    std::vector<uint8_t> sentPayload;
    ON_CALL(*suite->device.mockPeerComms, sendData(_, PktType::kPdnConnectionContext, _, _))
        .WillByDefault(testing::DoAll(
            testing::Invoke([&](const uint8_t*, PktType, const uint8_t* data, size_t len) {
                contextSends++;
                sentPayload.assign(data, data + len);
            }),
            Return(1)));

    // Full connect, initial send acked.
    suite->deliverHello(suite->outJack, suite->helloFrame(0xA1));
    suite->rdc.sync(&suite->device);
    ASSERT_EQ(contextSends, 1);
    suite->transport()->onSendResult(
        PktType::kPdnConnectionContext, peer, sentPayload.data(), sentPayload.size(), true);
    std::vector<uint8_t> ctx = pdnContextBytes(/*chainRole=*/1, /*userId=*/7, /*seqId=*/3);
    suite->transport()->deliverIncoming(
        PktType::kPdnConnectionContext, peer, ctx.data(), ctx.size());
    suite->rdc.sync(&suite->device);
    ASSERT_EQ(suite->rdc.getHelloLinkState(SerialIdentifier::OUTPUT_JACK),
              RemoteDeviceCoordinator::HelloLinkState::CONNECTED);

    // First retry resends; ack it so pending cannot mask the throttle.
    std::vector<uint8_t> retry1 = pdnContextBytes(/*chainRole=*/1, /*userId=*/7, /*seqId=*/4);
    suite->transport()->deliverIncoming(
        PktType::kPdnConnectionContext, peer, retry1.data(), retry1.size());
    ASSERT_EQ(contextSends, 2);
    suite->transport()->onSendResult(
        PktType::kPdnConnectionContext, peer, sentPayload.data(), sentPayload.size(), true);
    ASSERT_FALSE(suite->rdc.isContextSendPending(peer));

    // Second retry inside the window: throttled, exactly one resend total.
    std::vector<uint8_t> retry2 = pdnContextBytes(/*chainRole=*/1, /*userId=*/7, /*seqId=*/5);
    suite->transport()->deliverIncoming(
        PktType::kPdnConnectionContext, peer, retry2.data(), retry2.size());
    EXPECT_EQ(contextSends, 2);

    // Window expired: a genuine still-CONNECTING peer's next cycle resends again.
    suite->fakeClock->advance(RemoteDeviceCoordinator::CONTEXT_EXCHANGE_TIMEOUT_MS + 1);
    std::vector<uint8_t> retry3 = pdnContextBytes(/*chainRole=*/1, /*userId=*/7, /*seqId=*/6);
    suite->transport()->deliverIncoming(
        PktType::kPdnConnectionContext, peer, retry3.data(), retry3.size());
    EXPECT_EQ(contextSends, 3);
}

// The departed peer's chainRole must not survive link death: #156 reads
// getPeerChainRole during the NEXT peer's CONNECTING window, and the header
// promises 0 until that peer's context arrives.
inline void rdcLinkDeathClearsPeerChainRole(RDCHelloTests* suite) {
    const uint8_t peer[6] = {0xA1, 0x02, 0x03, 0x04, 0x05, 0x06};

    EXPECT_CALL(*suite->device.mockPeerComms, addEspNowPeer(_)).Times(testing::AnyNumber());

    suite->deliverHello(suite->outJack, suite->helloFrame(0xA1));
    suite->rdc.sync(&suite->device);
    std::vector<uint8_t> ctx = pdnContextBytes(/*chainRole=*/2, /*userId=*/7, /*seqId=*/3);
    suite->transport()->deliverIncoming(
        PktType::kPdnConnectionContext, peer, ctx.data(), ctx.size());
    suite->rdc.sync(&suite->device);
    ASSERT_EQ(suite->rdc.getHelloLinkState(SerialIdentifier::OUTPUT_JACK),
              RemoteDeviceCoordinator::HelloLinkState::CONNECTED);
    ASSERT_EQ(suite->rdc.getPeerChainRole(SerialIdentifier::OUTPUT_JACK), 2);

    suite->fakeClock->advance(RemoteDeviceCoordinator::HELLO_SILENT_LINK_MS + 1);
    suite->rdc.sync(&suite->device);
    ASSERT_EQ(suite->rdc.getHelloLinkState(SerialIdentifier::OUTPUT_JACK),
              RemoteDeviceCoordinator::HelloLinkState::IDLE);
    EXPECT_EQ(suite->rdc.getPeerChainRole(SerialIdentifier::OUTPUT_JACK), 0);
}

// Two fresh-seqId contexts from the same MAC in the same tick (before the
// Connecting -> Connected commit) must fire the game context callback once, not
// twice: the jack still reports CONNECTING in that window.
inline void rdcDuplicateContextSameTickFiresCallbackOnce(RDCHelloTests* suite) {
    const uint8_t peer[6] = {0xA1, 0x02, 0x03, 0x04, 0x05, 0x06};

    EXPECT_CALL(*suite->device.mockPeerComms, addEspNowPeer(_)).Times(testing::AnyNumber());
    int callbacks = 0;
    suite->rdc.setOnContextReceived(
        [&](SerialIdentifier, DeviceType, const uint8_t*, size_t) { callbacks++; });

    suite->deliverHello(suite->outJack, suite->helloFrame(0xA1));
    suite->rdc.sync(&suite->device);
    ASSERT_EQ(suite->rdc.getHelloLinkState(SerialIdentifier::OUTPUT_JACK),
              RemoteDeviceCoordinator::HelloLinkState::CONNECTING);

    std::vector<uint8_t> first = pdnContextBytes(/*chainRole=*/1, /*userId=*/7, /*seqId=*/3);
    std::vector<uint8_t> second = pdnContextBytes(/*chainRole=*/1, /*userId=*/7, /*seqId=*/4);
    suite->transport()->deliverIncoming(
        PktType::kPdnConnectionContext, peer, first.data(), first.size());
    suite->transport()->deliverIncoming(
        PktType::kPdnConnectionContext, peer, second.data(), second.size());
    EXPECT_EQ(callbacks, 1);

    suite->rdc.sync(&suite->device);
    EXPECT_EQ(suite->rdc.getHelloLinkState(SerialIdentifier::OUTPUT_JACK),
              RemoteDeviceCoordinator::HelloLinkState::CONNECTED);
    EXPECT_EQ(callbacks, 1);
}

// (f) With a byte callback set the driver exec() routes RX bytes to it and does
// NOT assemble strings; with no byte callback the legacy string path is intact.
inline void rdcHelloByteModeSuppressesStringAssembly() {
    NativeSerialDriver jack("f");
    int bytes = 0;
    bool stringFired = false;
    jack.setStringCallback([&](std::string) { stringFired = true; });
    jack.setByteCallback([&](uint8_t) { bytes++; });

    // Bytes that WOULD delimit a legacy string (STRING_START ... STRING_TERM).
    const std::string legacy = std::string(1, STRING_START) + "hello" + STRING_TERM;
    jack.injectBytes(std::vector<uint8_t>(legacy.begin(), legacy.end()));
    jack.exec();

    EXPECT_EQ(bytes, static_cast<int>(legacy.size()));
    EXPECT_FALSE(stringFired);

    // No byte callback: the legacy string delivery path still fires.
    NativeSerialDriver legacyJack("f-legacy");
    bool legacyString = false;
    legacyJack.setStringCallback([&](std::string) { legacyString = true; });
    legacyJack.injectInput(std::string(1, STRING_START) + "hi" + STRING_TERM);
    EXPECT_TRUE(legacyString);
}

// (g) RDC::sync() does not run the old handshake onStateLoop on a HELLO jack.
// Falsifiable: the same serial MAC drives the handshake to SEND_ID (which emits
// a kHandshakeCommand) on a plain RDC, but not on a HELLO-enabled one.
inline void rdcHelloSyncSkipsHandshakeOnStateLoop() {
    static uint8_t selfMac[6] = {0x99, 0x88, 0x77, 0x66, 0x55, 0x44};

    auto driveHandshakeMac = [](MockDevice& dev, RemoteDeviceCoordinator& coord,
                                int& handshakeSends) {
        ON_CALL(*dev.mockPeerComms, sendData(_, _, _, _)).WillByDefault(Return(1));
        ON_CALL(*dev.mockPeerComms, getMacAddress()).WillByDefault(Return(selfMac));
        ON_CALL(*dev.mockPeerComms, addEspNowPeer(_)).WillByDefault(Return(0));
        ON_CALL(*dev.mockPeerComms, sendData(_, PktType::kHandshakeCommand, _, _))
            .WillByDefault(testing::DoAll(
                testing::InvokeWithoutArgs([&handshakeSends]() { handshakeSends++; }),
                Return(1)));
        coord.setExternalConnectivityTask(true);
        coord.initialize(dev.wirelessManager, dev.serialManager, &dev);
    };

    // Control: a plain RDC advances the handshake on sync(), emitting a
    // kHandshakeCommand from OUTPUT_SEND_ID_STATE. Proves the input is live.
    MockDevice control;
    RemoteDeviceCoordinator controlRdc;
    int controlSends = 0;
    driveHandshakeMac(control, controlRdc, controlSends);
    control.outputJackSerial.stringCallback(SEND_MAC_ADDRESS + "AA:BB:CC:DD:EE:FF#1t1");
    controlRdc.sync(&control);
    EXPECT_GT(controlSends, 0);

    // Subject: a HELLO-enabled RDC skips the handshake onStateLoop, so the same
    // serial MAC never advances it to SEND_ID.
    MockDevice subject;
    RemoteDeviceCoordinator subjectRdc;
    int subjectSends = 0;
    driveHandshakeMac(subject, subjectRdc, subjectSends);
    subjectRdc.enableHelloConnectivity();
    subject.outputJackSerial.stringCallback(SEND_MAC_ADDRESS + "AA:BB:CC:DD:EE:FF#1t1");
    subjectRdc.sync(&subject);
    subjectRdc.sync(&subject);
    EXPECT_EQ(subjectSends, 0);
}

// ============================================
// Device-level chain state machine (#156)
// ============================================

// A framed HELLO carrying a source MAC and, optionally, an advertised head.
inline std::vector<uint8_t> chainHelloFrame(const uint8_t* source, const uint8_t* head) {
    HelloPayload hello{};
    memcpy(hello.source, source, 6);
    hello.deviceType = static_cast<uint8_t>(DeviceType::PDN);
    if (head != nullptr) memcpy(hello.headMac, head, 6);
    return encodeFramed(hello);
}

// Decodes a jack's emitted output back into a single HelloPayload.
inline HelloPayload parseEmittedHello(const std::string& out) {
    SerialFrameParser parser;
    HelloPayload got{};
    parser.setHelloFrameHandler([&](const HelloPayload& h) { got = h; });
    std::vector<uint8_t> bytes(out.begin(), out.end());
    parser.feed(bytes.data(), bytes.size());
    return got;
}

// Drives a jack Idle -> Connecting -> Connected. Each transition commits on a
// sync() tick, and the context exchange only advances a Connecting link.
inline void connectJack(RDCHelloTests* suite, NativeSerialDriver& jack,
                        SerialIdentifier id, const std::vector<uint8_t>& firstHello) {
    suite->deliverHello(jack, firstHello);
    suite->rdc.sync(&suite->device);
    suite->rdc.onContextExchangeComplete(id);
    suite->rdc.sync(&suite->device);
}

// Chain role is a local read of jack presence: OUTPUT connected = HEAD, INPUT
// connected = CHILD (dominates), neither = STANDALONE.
inline void rdcChainRoleReadsJackPresence(RDCHelloTests* suite) {
    EXPECT_EQ(suite->rdc.getChainRole(), ChainRole::STANDALONE);

    connectJack(suite, suite->outJack, SerialIdentifier::OUTPUT_JACK,
                suite->helloFrame(0xB1));
    EXPECT_EQ(suite->rdc.getChainRole(), ChainRole::HEAD);

    connectJack(suite, suite->inJack, SerialIdentifier::INPUT_JACK,
                suite->helloFrame(0xA1));
    EXPECT_EQ(suite->rdc.getChainRole(), ChainRole::CHILD);
}

// Downstream inheritance: adopt the upstream peer's effective head and
// re-advertise it. Head inheritance is synchronous on HELLO receipt.
inline void rdcChainInheritsAndReadvertisesHead(RDCHelloTests* suite) {
    const uint8_t upstream[6] = {0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F};
    const uint8_t head[6] = {0xF0, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5};

    connectJack(suite, suite->inJack, SerialIdentifier::INPUT_JACK,
                chainHelloFrame(upstream, head));
    ASSERT_NE(suite->rdc.getHeadMac(), nullptr);
    EXPECT_EQ(0, memcmp(suite->rdc.getHeadMac(), head, 6));

    suite->outJack.clearOutput();
    suite->rdc.emitHello();
    HelloPayload emitted = parseEmittedHello(suite->outJack.getOutput());
    EXPECT_EQ(0, memcmp(emitted.headMac, head, 6));
    EXPECT_EQ(0, memcmp(emitted.source, suite->localMac, 6));

    const uint8_t zero[6] = {0, 0, 0, 0, 0, 0};
    suite->deliverHello(suite->inJack, chainHelloFrame(upstream, zero));
    ASSERT_NE(suite->rdc.getHeadMac(), nullptr);
    EXPECT_EQ(0, memcmp(suite->rdc.getHeadMac(), upstream, 6));
}

// A structural head whose MAC is NOT the lowest still latches the ring, because
// inheritance carries its own MAC around the loop unchallenged.
inline void rdcChainNonLowestHeadDetectsRing(RDCHelloTests* suite) {
    int ringClosedCount = 0;
    suite->rdc.setOnRingClosed([&]() { ringClosedCount++; });

    connectJack(suite, suite->outJack, SerialIdentifier::OUTPUT_JACK,
                suite->helloFrame(0xB1));
    ASSERT_EQ(suite->rdc.getChainRole(), ChainRole::HEAD);

    const uint8_t lowNeighbour[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x01};
    suite->deliverHello(suite->inJack, chainHelloFrame(lowNeighbour, suite->localMac));

    EXPECT_EQ(ringClosedCount, 1);
    EXPECT_EQ(suite->rdc.getChainRole(), ChainRole::RING);
}

// Head transfer: when the INPUT peer drops while an OUTPUT peer remains, this
// device becomes the new head and clears its inherited (now phantom) head.
inline void rdcChainHeadTransferClearsInheritedHead(RDCHelloTests* suite) {
    const uint8_t upstream[6] = {0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F};
    const uint8_t head[6] = {0xF0, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5};
    const uint8_t downstream[6] = {0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6};

    connectJack(suite, suite->inJack, SerialIdentifier::INPUT_JACK,
                chainHelloFrame(upstream, head));
    connectJack(suite, suite->outJack, SerialIdentifier::OUTPUT_JACK,
                chainHelloFrame(downstream, nullptr));
    ASSERT_EQ(suite->rdc.getChainRole(), ChainRole::CHILD);
    ASSERT_NE(suite->rdc.getHeadMac(), nullptr);

    suite->fakeClock->advance(RemoteDeviceCoordinator::HELLO_SILENT_LINK_MS + 1);
    suite->deliverHello(suite->outJack, chainHelloFrame(downstream, nullptr));
    suite->rdc.sync(&suite->device);

    EXPECT_EQ(suite->rdc.getChainRole(), ChainRole::HEAD);
    EXPECT_EQ(suite->rdc.getHeadMac(), nullptr);

    suite->outJack.clearOutput();
    suite->rdc.emitHello();
    HelloPayload emitted = parseEmittedHello(suite->outJack.getOutput());
    const uint8_t zero[6] = {0, 0, 0, 0, 0, 0};
    EXPECT_EQ(0, memcmp(emitted.headMac, zero, 6));
}

// Phantom-head clear: a pure child whose upstream vanishes falls back to
// STANDALONE and stops advertising the vanished head.
inline void rdcChainPhantomHeadClearedOnSupplierLoss(RDCHelloTests* suite) {
    const uint8_t upstream[6] = {0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F};
    const uint8_t head[6] = {0xF0, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5};

    connectJack(suite, suite->inJack, SerialIdentifier::INPUT_JACK,
                chainHelloFrame(upstream, head));
    ASSERT_NE(suite->rdc.getHeadMac(), nullptr);

    suite->fakeClock->advance(RemoteDeviceCoordinator::HELLO_SILENT_LINK_MS + 1);
    suite->rdc.sync(&suite->device);

    EXPECT_EQ(suite->rdc.getChainRole(), ChainRole::STANDALONE);
    EXPECT_EQ(suite->rdc.getHeadMac(), nullptr);

    suite->outJack.clearOutput();
    suite->rdc.emitHello();
    HelloPayload emitted = parseEmittedHello(suite->outJack.getOutput());
    const uint8_t zero[6] = {0, 0, 0, 0, 0, 0};
    EXPECT_EQ(0, memcmp(emitted.headMac, zero, 6));
}

// Ring opens when the INPUT peer drops: the latch clears and the role falls back
// to a plain read of jack presence.
inline void rdcChainRingOpensOnInputDrop(RDCHelloTests* suite) {
    connectJack(suite, suite->outJack, SerialIdentifier::OUTPUT_JACK,
                suite->helloFrame(0xB1));
    const uint8_t lowNeighbour[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x01};
    suite->deliverHello(suite->inJack, chainHelloFrame(lowNeighbour, suite->localMac));
    suite->rdc.sync(&suite->device);
    ASSERT_EQ(suite->rdc.getChainRole(), ChainRole::RING);

    suite->fakeClock->advance(RemoteDeviceCoordinator::HELLO_SILENT_LINK_MS + 1);
    suite->deliverHello(suite->outJack, suite->helloFrame(0xB1));
    suite->rdc.sync(&suite->device);

    EXPECT_EQ(suite->rdc.getChainRole(), ChainRole::HEAD);
}

// Ring opens when the peer drops on the OUTPUT side, not just the INPUT side: a
// closed loop breaks whichever link fails.
inline void rdcChainRingOpensOnOutputDrop(RDCHelloTests* suite) {
    connectJack(suite, suite->outJack, SerialIdentifier::OUTPUT_JACK,
                suite->helloFrame(0xB1));
    const uint8_t lowNeighbour[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x01};
    suite->deliverHello(suite->inJack, chainHelloFrame(lowNeighbour, suite->localMac));
    suite->rdc.sync(&suite->device);
    suite->rdc.onContextExchangeComplete(SerialIdentifier::INPUT_JACK);
    suite->rdc.sync(&suite->device);
    ASSERT_EQ(suite->rdc.getChainRole(), ChainRole::RING);

    suite->fakeClock->advance(RemoteDeviceCoordinator::HELLO_SILENT_LINK_MS + 1);
    suite->deliverHello(suite->inJack, chainHelloFrame(lowNeighbour, suite->localMac));
    suite->rdc.sync(&suite->device);

    EXPECT_EQ(suite->rdc.getChainRole(), ChainRole::CHILD);
}

// Non-adjacent break: the INPUT peer stays put but, having lost ITS own upstream,
// stops echoing our head and advertises itself. The head returning on INPUT no
// longer matches ours, so the ring must open even though neither of our links
// dropped — the break was further around the loop.
inline void rdcChainRingOpensWhenReturnedHeadChanges(RDCHelloTests* suite) {
    connectJack(suite, suite->outJack, SerialIdentifier::OUTPUT_JACK,
                suite->helloFrame(0xB1));
    const uint8_t upstream[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x01};
    connectJack(suite, suite->inJack, SerialIdentifier::INPUT_JACK,
                chainHelloFrame(upstream, suite->localMac));
    ASSERT_EQ(suite->rdc.getChainRole(), ChainRole::RING);

    // Same upstream source, now advertising itself as head (absent head_mac).
    suite->deliverHello(suite->inJack, chainHelloFrame(upstream, nullptr));
    suite->rdc.sync(&suite->device);

    EXPECT_NE(suite->rdc.getChainRole(), ChainRole::RING);
    ASSERT_NE(suite->rdc.getHeadMac(), nullptr);
    EXPECT_EQ(0, memcmp(suite->rdc.getHeadMac(), upstream, 6));
}

// Merge-in-the-middle: two already-connected sub-chains join into a ring, so our
// own head first returns on an INPUT jack that is ALREADY connected. Latching must
// gate on OUTPUT presence, not role==HEAD (which demands a disconnected INPUT).
inline void rdcChainRingLatchesOnMergeWithConnectedInput(RDCHelloTests* suite) {
    connectJack(suite, suite->outJack, SerialIdentifier::OUTPUT_JACK,
                suite->helloFrame(0xB1));
    const uint8_t upstream[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x01};
    const uint8_t foreignHead[6] = {0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC};
    // INPUT fully connects first, under some other head (role CHILD).
    connectJack(suite, suite->inJack, SerialIdentifier::INPUT_JACK,
                chainHelloFrame(upstream, foreignHead));
    ASSERT_EQ(suite->rdc.getChainRole(), ChainRole::CHILD);

    // The far side of the merge now carries our own MAC back around.
    suite->deliverHello(suite->inJack, chainHelloFrame(upstream, suite->localMac));
    suite->rdc.sync(&suite->device);

    EXPECT_EQ(suite->rdc.getChainRole(), ChainRole::RING);
    // Latching means we are the head: the head adopted while the ring was forming
    // must be dropped, not advertised on into the closed ring.
    EXPECT_EQ(suite->rdc.getHeadMac(), nullptr);
}

// The FDN secondary input jack is a symbol link, not part of the chain, so its
// loss must not open a ring closed over the INPUT/OUTPUT jacks.
inline void rdcChainSecondaryJackLossKeepsRing(RDCHelloTests* suite) {
    connectJack(suite, suite->outJack, SerialIdentifier::OUTPUT_JACK,
                suite->helloFrame(0xB1));
    const uint8_t upstream[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x01};
    suite->deliverHello(suite->inJack, chainHelloFrame(upstream, suite->localMac));
    suite->rdc.sync(&suite->device);
    ASSERT_EQ(suite->rdc.getChainRole(), ChainRole::RING);

    connectJack(suite, suite->secondaryJack, SerialIdentifier::INPUT_JACK_SECONDARY,
                suite->helloFrame(0xD1));
    ASSERT_EQ(suite->rdc.getChainRole(), ChainRole::RING);

    // Let ONLY the secondary jack silent-link out; keep the two chain jacks alive.
    suite->fakeClock->advance(RemoteDeviceCoordinator::HELLO_SILENT_LINK_MS + 1);
    suite->deliverHello(suite->outJack, suite->helloFrame(0xB1));
    suite->deliverHello(suite->inJack, chainHelloFrame(upstream, suite->localMac));
    suite->rdc.sync(&suite->device);

    EXPECT_EQ(suite->rdc.getChainRole(), ChainRole::RING);
}

// A different source MAC on a still-live INPUT jack (peer swapped before the
// silent-link watchdog fired) tears the link down and re-derives: the stale ring
// latch and the head inherited from the vanished peer cannot survive.
inline void rdcChainPeerSwapClearsStaleRing(RDCHelloTests* suite) {
    connectJack(suite, suite->outJack, SerialIdentifier::OUTPUT_JACK,
                suite->helloFrame(0xB1));
    const uint8_t peerA[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x01};
    suite->deliverHello(suite->inJack, chainHelloFrame(peerA, suite->localMac));
    suite->rdc.sync(&suite->device);
    ASSERT_EQ(suite->rdc.getChainRole(), ChainRole::RING);

    const uint8_t peerB[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x02};
    const uint8_t foreignHead[6] = {0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC};
    suite->deliverHello(suite->inJack, chainHelloFrame(peerB, foreignHead));
    EXPECT_NE(suite->rdc.getChainRole(), ChainRole::RING);

    // getHeadMac only surfaces the adopted head once the fresh link completes.
    suite->rdc.sync(&suite->device);
    suite->rdc.onContextExchangeComplete(SerialIdentifier::INPUT_JACK);
    suite->rdc.sync(&suite->device);
    ASSERT_NE(suite->rdc.getHeadMac(), nullptr);
    EXPECT_EQ(0, memcmp(suite->rdc.getHeadMac(), foreignHead, 6));
}

// Non-adjacent break where the replacement head has a HIGHER MAC than ours:
// lower-MAC stand-down alone would hold the latch forever. The latch is
// evidence-based — once our own MAC stops returning on INPUT for the evidence
// window, the device must stand down and adopt the propagated head.
inline void rdcChainRingYieldsToHigherHeadAfterEvidenceTimeout(RDCHelloTests* suite) {
    connectJack(suite, suite->outJack, SerialIdentifier::OUTPUT_JACK,
                suite->helloFrame(0xB1));
    const uint8_t upstream[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x01};
    connectJack(suite, suite->inJack, SerialIdentifier::INPUT_JACK,
                chainHelloFrame(upstream, suite->localMac));
    ASSERT_EQ(suite->rdc.getChainRole(), ChainRole::RING);

    const uint8_t higherHead[6] = {0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE};

    // Inside the evidence window a higher claim reads as a merge transient:
    // the latch holds.
    suite->deliverHello(suite->inJack, chainHelloFrame(upstream, higherHead));
    suite->rdc.sync(&suite->device);
    ASSERT_EQ(suite->rdc.getChainRole(), ChainRole::RING);

    // Both links stay alive at HELLO cadence but no self-return ever arrives;
    // past the window the latch must yield.
    unsigned long elapsed = 0;
    while (elapsed <= RemoteDeviceCoordinator::RING_EVIDENCE_TIMEOUT_MS) {
        suite->fakeClock->advance(RemoteDeviceCoordinator::HELLO_CADENCE_MS);
        elapsed += RemoteDeviceCoordinator::HELLO_CADENCE_MS;
        suite->deliverHello(suite->outJack, suite->helloFrame(0xB1));
        suite->deliverHello(suite->inJack, chainHelloFrame(upstream, higherHead));
        suite->rdc.sync(&suite->device);
    }

    EXPECT_NE(suite->rdc.getChainRole(), ChainRole::RING);
    ASSERT_NE(suite->rdc.getHeadMac(), nullptr);
    EXPECT_EQ(0, memcmp(suite->rdc.getHeadMac(), higherHead, 6));

    suite->outJack.clearOutput();
    suite->rdc.emitHello();
    HelloPayload emitted = parseEmittedHello(suite->outJack.getOutput());
    EXPECT_EQ(0, memcmp(emitted.headMac, higherHead, 6));
}

// One node bundling an RDC, its device and jacks, for the two-live-RDC ring test.
struct ChainRingNode {
    /// Wires the node's RDC to its own jacks under the given MAC.
    explicit ChainRingNode(const uint8_t m[6]) {
        memcpy(mac, m, 6);
        wireRadioDefaults(device, mac);
        device.serialManager->setOutputJack(&out);
        device.serialManager->setInputJack(&in);
        rdc.setExternalConnectivityTask(true);
        rdc.initialize(device.wirelessManager, device.serialManager, &device);
        rdc.setOnRingClosed([this]() { ringClosedCount++; });
        rdc.enableHelloConnectivity();
    }

    MockDevice device;
    NativeSerialDriver out{"ring-out"};
    NativeSerialDriver in{"ring-in"};
    RemoteDeviceCoordinator rdc;
    uint8_t mac[6] = {};
    int ringClosedCount = 0;
};

// Two live RDCs cross-wired into a 2-device ring (A.out<->B.in, B.out<->A.in). A's
// head MAC propagates to B by inheritance, returns on A's INPUT jack, and latches
// the ring on A with no coordination message and no MAC election.
inline void rdcChainTwoNodeRingCloses() {
    FakePlatformClock clock;
    SimpleTimer::setPlatformClock(&clock);
    clock.setTime(1000);

    const uint8_t macA[6] = {0xAA, 0x00, 0x00, 0x00, 0x00, 0x02};
    const uint8_t macB[6] = {0x01, 0x00, 0x00, 0x00, 0x00, 0x03};
    ChainRingNode A(macA);
    ChainRingNode B(macB);

    // Cable 1: A.out -> B.in. B inherits A as its head.
    A.rdc.emitHello();
    pumpCable(A.out, B.in);
    B.rdc.sync(&B.device);

    // A.out hears B on the return leg; bring both ends to Connected so A is the
    // structural HEAD and B a CHILD.
    B.rdc.emitHello();
    pumpCable(B.in, A.out);
    A.rdc.sync(&A.device);
    A.rdc.onContextExchangeComplete(SerialIdentifier::OUTPUT_JACK);
    A.rdc.sync(&A.device);
    B.rdc.onContextExchangeComplete(SerialIdentifier::INPUT_JACK);
    B.rdc.sync(&B.device);
    ASSERT_EQ(A.rdc.getChainRole(), ChainRole::HEAD);
    ASSERT_EQ(B.rdc.getChainRole(), ChainRole::CHILD);
    ASSERT_NE(B.rdc.getHeadMac(), nullptr);
    EXPECT_EQ(0, memcmp(B.rdc.getHeadMac(), macA, 6));

    // Cable 2 closes the ring: B.out -> A.in carries B's HELLO advertising head=A.
    B.out.clearOutput();
    B.rdc.emitHello();
    pumpCable(B.out, A.in);

    EXPECT_EQ(A.ringClosedCount, 1);
    EXPECT_EQ(A.rdc.getChainRole(), ChainRole::RING);

    SimpleTimer::setPlatformClock(nullptr);
}

// Symmetric double-latch: both devices of a 2-ring latch RING in the same
// exchange (each hears its own MAC return before reading the other's claim).
// Lower MAC wins the head conflict: only the higher-MAC device stands down, the
// lower keeps its latch, and ringClosed never re-fires once settled.
inline void rdcChainDualLatchSettlesByLowerMac() {
    FakePlatformClock clock;
    SimpleTimer::setPlatformClock(&clock);
    clock.setTime(1000);

    const uint8_t macA[6] = {0x01, 0x00, 0x00, 0x00, 0x00, 0x02};  // lower: keeps latch
    const uint8_t macB[6] = {0xAA, 0x00, 0x00, 0x00, 0x00, 0x03};
    ChainRingNode A(macA);
    ChainRingNode B(macB);

    // Bring both OUTPUT jacks to Connected: each hears the other's HELLO on the
    // return leg of its own out-cable.
    B.rdc.emitHello();
    pumpCable(B.in, A.out);
    A.rdc.sync(&A.device);
    A.rdc.onContextExchangeComplete(SerialIdentifier::OUTPUT_JACK);
    A.rdc.sync(&A.device);
    A.rdc.emitHello();
    pumpCable(A.in, B.out);
    B.rdc.sync(&B.device);
    B.rdc.onContextExchangeComplete(SerialIdentifier::OUTPUT_JACK);
    B.rdc.sync(&B.device);
    A.out.clearOutput();
    A.in.clearOutput();
    B.out.clearOutput();
    B.in.clearOutput();

    // Both latch in one exchange: each INPUT delivers the device's own MAC as
    // the returned head while the other side is doing the same.
    deliverFrame(A.in, chainHelloFrame(macB, macA));
    deliverFrame(B.in, chainHelloFrame(macA, macB));
    ASSERT_EQ(A.rdc.getChainRole(), ChainRole::RING);
    ASSERT_EQ(B.rdc.getChainRole(), ChainRole::RING);
    ASSERT_EQ(A.ringClosedCount, 1);
    ASSERT_EQ(B.ringClosedCount, 1);

    // Complete both INPUT links so advancing time below exercises only the ring
    // evidence window, not the context-exchange timeout.
    A.rdc.sync(&A.device);
    A.rdc.onContextExchangeComplete(SerialIdentifier::INPUT_JACK);
    A.rdc.sync(&A.device);
    B.rdc.sync(&B.device);
    B.rdc.onContextExchangeComplete(SerialIdentifier::INPUT_JACK);
    B.rdc.sync(&B.device);

    // Emit-before-deliver models the real concurrency: both HELLOs are in
    // flight before either side processes one. Must converge, not oscillate,
    // and running past the evidence window pins that the survivor's own
    // self-returns keep its latch alive.
    for (int i = 0; i < 40; ++i) {
        clock.advance(RemoteDeviceCoordinator::HELLO_CADENCE_MS);
        A.rdc.emitHello();
        B.rdc.emitHello();
        pumpCable(A.out, B.in);
        pumpCable(B.out, A.in);
        pumpCable(A.in, B.out);  // reverse legs keep the OUT jacks alive
        pumpCable(B.in, A.out);
        A.rdc.sync(&A.device);
        B.rdc.sync(&B.device);
    }

    EXPECT_EQ(A.ringClosedCount, 1);
    EXPECT_EQ(B.ringClosedCount, 1);
    EXPECT_EQ(A.rdc.getChainRole(), ChainRole::RING);
    EXPECT_EQ(B.rdc.getChainRole(), ChainRole::CHILD);

    SimpleTimer::setPlatformClock(nullptr);
}

// ============================================
// Head roster management (#158)
// ============================================

// Records every send of one PktType: count, last destination, last payload.
struct RosterSendCapture {
    int count = 0;
    std::array<uint8_t, 6> lastDst{};
    std::vector<uint8_t> lastPayload;
};

inline void captureSends(RDCHelloTests* suite, PktType type, RosterSendCapture& capture) {
    ON_CALL(*suite->device.mockPeerComms, sendData(_, type, _, _))
        .WillByDefault(testing::DoAll(
            testing::Invoke(
                [&capture](const uint8_t* dst, PktType, const uint8_t* data, size_t len) {
                    capture.count++;
                    memcpy(capture.lastDst.data(), dst, 6);
                    capture.lastPayload.assign(data, data + len);
                }),
            Return(1)));
}

inline std::vector<uint8_t> announceBytes(const uint8_t* upstream, uint8_t seqId) {
    ConnectionAnnouncePayload payload{};
    payload.seqId = seqId;
    memcpy(payload.upstreamMac, upstream, 6);
    std::vector<uint8_t> bytes(sizeof(payload));
    memcpy(bytes.data(), &payload, sizeof(payload));
    return bytes;
}

inline std::vector<uint8_t> disconnectReportBytes(const uint8_t* mac, uint8_t seqId) {
    DisconnectReportPayload payload{};
    payload.seqId = seqId;
    memcpy(payload.disconnectedMac, mac, 6);
    std::vector<uint8_t> bytes(sizeof(payload));
    memcpy(bytes.data(), &payload, sizeof(payload));
    return bytes;
}

// Each entry is a (member, upstream) pair mirroring the roster edge it carries.
inline std::vector<uint8_t> headTransferBytes(
    const std::vector<std::pair<std::array<uint8_t, 6>, std::array<uint8_t, 6>>>& entries,
    uint8_t seqId) {
    HeadTransferPayload payload{};
    payload.seqId = seqId;
    payload.memberCount = static_cast<uint8_t>(entries.size());
    for (size_t i = 0; i < entries.size() && i < MAX_CHAIN_MEMBERS; ++i) {
        memcpy(payload.memberMacs[i], entries[i].first.data(), 6);
        memcpy(payload.upstreamMacs[i], entries[i].second.data(), 6);
    }
    std::vector<uint8_t> bytes(sizeof(payload));
    memcpy(bytes.data(), &payload, sizeof(payload));
    return bytes;
}

// Join flow (#144 steps 3-6): the announce goes to the head only once the
// upstream exchange completes, names the upstream neighbour, and the HELLO
// confirmed bit stays 0 until the announce's SEND_SUCCESS lands.
inline void rdcJoinAnnouncesToHeadAndGatesConfirmed(RDCHelloTests* suite) {
    const uint8_t upstream[6] = {0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F};
    const uint8_t head[6] = {0xF0, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5};

    EXPECT_CALL(*suite->device.mockPeerComms, addEspNowPeer(_)).Times(testing::AnyNumber());
    RosterSendCapture announce;
    captureSends(suite, PktType::kConnectionAnnounce, announce);

    // Upstream HELLO arrives: head adopted, but the exchange is incomplete, so
    // no announce yet.
    suite->deliverHello(suite->inJack, chainHelloFrame(upstream, head));
    suite->rdc.sync(&suite->device);
    EXPECT_EQ(announce.count, 0);

    // Upstream context lands: the join moment.
    std::vector<uint8_t> ctx = pdnContextBytes(/*chainRole=*/1, /*userId=*/7, /*seqId=*/3);
    suite->transport()->deliverIncoming(
        PktType::kPdnConnectionContext, upstream, ctx.data(), ctx.size());
    suite->rdc.sync(&suite->device);
    ASSERT_EQ(announce.count, 1);
    EXPECT_EQ(0, memcmp(announce.lastDst.data(), head, 6));
    ConnectionAnnouncePayload sent{};
    ASSERT_EQ(announce.lastPayload.size(), sizeof(sent));
    memcpy(&sent, announce.lastPayload.data(), sizeof(sent));
    EXPECT_EQ(0, memcmp(sent.upstreamMac, upstream, 6));

    // confirmed stays down until the announce is delivered.
    suite->outJack.clearOutput();
    suite->rdc.emitHello();
    EXPECT_EQ(parseEmittedHello(suite->outJack.getOutput()).confirmed, 0);

    suite->transport()->onSendResult(PktType::kConnectionAnnounce, head,
                                     announce.lastPayload.data(),
                                     announce.lastPayload.size(), true);
    suite->outJack.clearOutput();
    suite->rdc.emitHello();
    EXPECT_EQ(parseEmittedHello(suite->outJack.getOutput()).confirmed, 1);
}

// A head change drops confirmed to 0 and re-announces to the new head; confirmed
// rises again only on the new announce's delivery.
inline void rdcHeadChangeDropsConfirmedAndReannounces(RDCHelloTests* suite) {
    const uint8_t upstream[6] = {0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F};
    const uint8_t head[6] = {0xF0, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5};
    const uint8_t newHead[6] = {0xE0, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5};

    EXPECT_CALL(*suite->device.mockPeerComms, addEspNowPeer(_)).Times(testing::AnyNumber());
    RosterSendCapture announce;
    captureSends(suite, PktType::kConnectionAnnounce, announce);

    suite->deliverHello(suite->inJack, chainHelloFrame(upstream, head));
    suite->rdc.sync(&suite->device);
    std::vector<uint8_t> ctx = pdnContextBytes(/*chainRole=*/1, /*userId=*/7, /*seqId=*/3);
    suite->transport()->deliverIncoming(
        PktType::kPdnConnectionContext, upstream, ctx.data(), ctx.size());
    suite->rdc.sync(&suite->device);
    ASSERT_EQ(announce.count, 1);
    suite->transport()->onSendResult(PktType::kConnectionAnnounce, head,
                                     announce.lastPayload.data(),
                                     announce.lastPayload.size(), true);
    suite->outJack.clearOutput();
    suite->rdc.emitHello();
    ASSERT_EQ(parseEmittedHello(suite->outJack.getOutput()).confirmed, 1);

    // The upstream now advertises a different head.
    suite->deliverHello(suite->inJack, chainHelloFrame(upstream, newHead));
    ASSERT_EQ(announce.count, 2);
    EXPECT_EQ(0, memcmp(announce.lastDst.data(), newHead, 6));
    suite->outJack.clearOutput();
    suite->rdc.emitHello();
    EXPECT_EQ(parseEmittedHello(suite->outJack.getOutput()).confirmed, 0);

    suite->transport()->onSendResult(PktType::kConnectionAnnounce, newHead,
                                     announce.lastPayload.data(),
                                     announce.lastPayload.size(), true);
    suite->outJack.clearOutput();
    suite->rdc.emitHello();
    EXPECT_EQ(parseEmittedHello(suite->outJack.getOutput()).confirmed, 1);
}

// Head side: announces build the roster (member -> upstream), duplicates are
// absorbed, and a disconnect report prunes the departed member AND everything
// whose upstream chain passes through it.
inline void rdcHeadBuildsAndPrunesRoster(RDCHelloTests* suite) {
    const uint8_t c1[6] = {0xC1, 0x02, 0x03, 0x04, 0x05, 0x06};
    const uint8_t c2[6] = {0xC2, 0x02, 0x03, 0x04, 0x05, 0x06};
    const uint8_t c3[6] = {0xC3, 0x02, 0x03, 0x04, 0x05, 0x06};

    EXPECT_CALL(*suite->device.mockPeerComms, addEspNowPeer(_)).Times(testing::AnyNumber());
    int membershipChanges = 0;
    suite->rdc.setOnMembershipChange([&]() { membershipChanges++; });

    connectJack(suite, suite->outJack, SerialIdentifier::OUTPUT_JACK, suite->helloFrame(0xB1));
    ASSERT_EQ(suite->rdc.getChainRole(), ChainRole::HEAD);

    // HEAD <- c1 <- c2 <- c3
    std::vector<uint8_t> a1 = announceBytes(suite->localMac, 1);
    std::vector<uint8_t> a2 = announceBytes(c1, 1);
    std::vector<uint8_t> a3 = announceBytes(c2, 1);
    suite->transport()->deliverIncoming(PktType::kConnectionAnnounce, c1, a1.data(), a1.size());
    suite->transport()->deliverIncoming(PktType::kConnectionAnnounce, c2, a2.data(), a2.size());
    suite->transport()->deliverIncoming(PktType::kConnectionAnnounce, c3, a3.data(), a3.size());
    EXPECT_EQ(membershipChanges, 3);
    EXPECT_EQ(suite->rdc.getChainMembers().size(), 3u);

    // A re-announce with identical content is not a membership change.
    std::vector<uint8_t> dup = announceBytes(c1, 2);
    suite->transport()->deliverIncoming(PktType::kConnectionAnnounce, c2, dup.data(), dup.size());
    EXPECT_EQ(membershipChanges, 3);

    // c2 departs: c3 hangs off c2, so both go.
    std::vector<uint8_t> report = disconnectReportBytes(c2, 1);
    suite->transport()->deliverIncoming(PktType::kDisconnectReport, c1, report.data(), report.size());
    EXPECT_EQ(membershipChanges, 4);
    std::vector<std::array<uint8_t, 6>> members = suite->rdc.getChainMembers();
    ASSERT_EQ(members.size(), 1u);
    EXPECT_EQ(0, memcmp(members[0].data(), c1, 6));
}

// A child reports a downstream (OUTPUT) departure to its head — and, not being
// a head, ignores stray roster traffic addressed to it.
inline void rdcChildReportsDownstreamLossToHead(RDCHelloTests* suite) {
    const uint8_t upstream[6] = {0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F};
    const uint8_t head[6] = {0xF0, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5};
    const uint8_t downstream[6] = {0xD1, 0x02, 0x03, 0x04, 0x05, 0x06};

    EXPECT_CALL(*suite->device.mockPeerComms, addEspNowPeer(_)).Times(testing::AnyNumber());
    RosterSendCapture report;
    captureSends(suite, PktType::kDisconnectReport, report);
    int membershipChanges = 0;
    suite->rdc.setOnMembershipChange([&]() { membershipChanges++; });

    connectJack(suite, suite->inJack, SerialIdentifier::INPUT_JACK,
                chainHelloFrame(upstream, head));
    connectJack(suite, suite->outJack, SerialIdentifier::OUTPUT_JACK,
                suite->helloFrame(0xD1));
    ASSERT_EQ(suite->rdc.getChainRole(), ChainRole::CHILD);

    // Not the head: an announce reaching this device must not grow a roster.
    std::vector<uint8_t> stray = announceBytes(head, 1);
    suite->transport()->deliverIncoming(PktType::kConnectionAnnounce, downstream,
                                        stray.data(), stray.size());
    EXPECT_EQ(membershipChanges, 0);
    EXPECT_TRUE(suite->rdc.getChainMembers().empty());

    // Only the downstream cable dies; the upstream HELLO stays fresh.
    suite->fakeClock->advance(RemoteDeviceCoordinator::HELLO_SILENT_LINK_MS + 1);
    suite->deliverHello(suite->inJack, chainHelloFrame(upstream, head));
    suite->rdc.sync(&suite->device);
    ASSERT_EQ(suite->rdc.getHelloLinkState(SerialIdentifier::OUTPUT_JACK),
              RemoteDeviceCoordinator::HelloLinkState::IDLE);

    ASSERT_EQ(report.count, 1);
    EXPECT_EQ(0, memcmp(report.lastDst.data(), head, 6));
    DisconnectReportPayload sent{};
    ASSERT_EQ(report.lastPayload.size(), sizeof(sent));
    memcpy(&sent, report.lastPayload.data(), sizeof(sent));
    EXPECT_EQ(0, memcmp(sent.disconnectedMac, downstream, 6));
}

// A latched ring device is its own head: it never announces to itself, and a
// downstream loss edits its roster in place instead of sending a report.
inline void rdcRingLatchedNeverAnnouncesOrReportsToSelf(RDCHelloTests* suite) {
    const uint8_t lowNeighbour[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x01};
    const uint8_t downstream[6] = {0xB1, 0x02, 0x03, 0x04, 0x05, 0x06};

    EXPECT_CALL(*suite->device.mockPeerComms, addEspNowPeer(_)).Times(testing::AnyNumber());
    RosterSendCapture announce;
    RosterSendCapture report;
    captureSends(suite, PktType::kConnectionAnnounce, announce);
    captureSends(suite, PktType::kDisconnectReport, report);
    int membershipChanges = 0;
    suite->rdc.setOnMembershipChange([&]() { membershipChanges++; });

    connectJack(suite, suite->outJack, SerialIdentifier::OUTPUT_JACK, suite->helloFrame(0xB1));
    suite->deliverHello(suite->inJack, chainHelloFrame(lowNeighbour, suite->localMac));
    ASSERT_EQ(suite->rdc.getChainRole(), ChainRole::RING);

    // The upstream exchange completing (the usual announce moment) must send
    // nothing: this device holds no head above itself.
    std::vector<uint8_t> ctx = pdnContextBytes(/*chainRole=*/1, /*userId=*/7, /*seqId=*/9);
    suite->transport()->deliverIncoming(
        PktType::kPdnConnectionContext, lowNeighbour, ctx.data(), ctx.size());
    suite->rdc.sync(&suite->device);
    EXPECT_EQ(announce.count, 0);

    // The ring's members announce to it like to any head.
    std::vector<uint8_t> join = announceBytes(suite->localMac, 1);
    suite->transport()->deliverIncoming(PktType::kConnectionAnnounce, downstream,
                                        join.data(), join.size());
    ASSERT_EQ(membershipChanges, 1);
    EXPECT_EQ(suite->rdc.getChainMembers().size(), 1u);

    // Downstream cable dies: roster edited in place, no packet to self.
    suite->fakeClock->advance(RemoteDeviceCoordinator::HELLO_SILENT_LINK_MS + 1);
    suite->deliverHello(suite->inJack, chainHelloFrame(lowNeighbour, suite->localMac));
    suite->rdc.sync(&suite->device);
    ASSERT_EQ(suite->rdc.getHelloLinkState(SerialIdentifier::OUTPUT_JACK),
              RemoteDeviceCoordinator::HelloLinkState::IDLE);
    EXPECT_EQ(report.count, 0);
    EXPECT_EQ(membershipChanges, 2);
}

// Demotion: a head that adopts a new head above it hands its roster over in a
// single unicast and clears it locally.
inline void rdcDemotedHeadTransfersRoster(RDCHelloTests* suite) {
    const uint8_t memberA[6] = {0xA1, 0x02, 0x03, 0x04, 0x05, 0x06};
    const uint8_t memberB[6] = {0xB2, 0x02, 0x03, 0x04, 0x05, 0x06};
    const uint8_t newHead[6] = {0x11, 0x11, 0x11, 0x11, 0x11, 0x11};

    EXPECT_CALL(*suite->device.mockPeerComms, addEspNowPeer(_)).Times(testing::AnyNumber());
    RosterSendCapture transfer;
    captureSends(suite, PktType::kHeadTransfer, transfer);
    int membershipChanges = 0;
    suite->rdc.setOnMembershipChange([&]() { membershipChanges++; });

    connectJack(suite, suite->outJack, SerialIdentifier::OUTPUT_JACK, suite->helloFrame(0xB1));
    ASSERT_EQ(suite->rdc.getChainRole(), ChainRole::HEAD);
    std::vector<uint8_t> a1 = announceBytes(suite->localMac, 1);
    std::vector<uint8_t> a2 = announceBytes(memberA, 1);
    suite->transport()->deliverIncoming(PktType::kConnectionAnnounce, memberA, a1.data(), a1.size());
    suite->transport()->deliverIncoming(PktType::kConnectionAnnounce, memberB, a2.data(), a2.size());
    ASSERT_EQ(suite->rdc.getChainMembers().size(), 2u);
    ASSERT_EQ(membershipChanges, 2);

    // A standalone head plugs in above: its HELLO makes it our effective head.
    suite->deliverHello(suite->inJack, chainHelloFrame(newHead, nullptr));

    ASSERT_EQ(transfer.count, 1);
    EXPECT_EQ(0, memcmp(transfer.lastDst.data(), newHead, 6));
    HeadTransferPayload sent{};
    ASSERT_EQ(transfer.lastPayload.size(), sizeof(sent));
    memcpy(&sent, transfer.lastPayload.data(), sizeof(sent));
    ASSERT_EQ(sent.memberCount, 2);
    // Map order is byte-lexicographic: memberA (0xA1...) precedes memberB (0xB2...).
    EXPECT_EQ(0, memcmp(sent.memberMacs[0], memberA, 6));
    EXPECT_EQ(0, memcmp(sent.memberMacs[1], memberB, 6));
    // Each member travels with its recorded upstream, preserving chain order.
    EXPECT_EQ(0, memcmp(sent.upstreamMacs[0], suite->localMac, 6));
    EXPECT_EQ(0, memcmp(sent.upstreamMacs[1], memberA, 6));
    EXPECT_EQ(membershipChanges, 3);
    EXPECT_TRUE(suite->rdc.getChainMembers().empty());
}

// A ring's closing cable dying is the OUTPUT link to the device's own head;
// reporting that "loss" would name the head itself. The head rejects such a
// report at its trust boundary, so sending one would only be dead traffic.
inline void rdcOutputLossOfOwnHeadSendsNoReport(RDCHelloTests* suite) {
    const uint8_t upstream[6] = {0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F};
    const uint8_t head[6] = {0xF0, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5};

    EXPECT_CALL(*suite->device.mockPeerComms, addEspNowPeer(_)).Times(testing::AnyNumber());
    EXPECT_CALL(*suite->device.mockPeerComms, removeEspNowPeer(_)).Times(testing::AnyNumber());
    RosterSendCapture report;
    captureSends(suite, PktType::kDisconnectReport, report);

    connectJack(suite, suite->inJack, SerialIdentifier::INPUT_JACK,
                chainHelloFrame(upstream, head));
    // The OUTPUT peer IS the head: this device closes the loop back to it.
    connectJack(suite, suite->outJack, SerialIdentifier::OUTPUT_JACK,
                chainHelloFrame(head, nullptr));
    ASSERT_EQ(suite->rdc.getChainRole(), ChainRole::CHILD);

    // Only the closing cable dies; the upstream HELLO stays fresh.
    suite->fakeClock->advance(RemoteDeviceCoordinator::HELLO_SILENT_LINK_MS + 1);
    suite->deliverHello(suite->inJack, chainHelloFrame(upstream, head));
    suite->rdc.sync(&suite->device);
    ASSERT_EQ(suite->rdc.getHelloLinkState(SerialIdentifier::OUTPUT_JACK),
              RemoteDeviceCoordinator::HelloLinkState::IDLE);

    EXPECT_EQ(report.count, 0);
}

// Belt at the trust boundary: an inbound report naming this device would wipe
// the whole roster via the subtree walk, so it is dropped outright.
inline void rdcHeadIgnoresSelfDisconnectReport(RDCHelloTests* suite) {
    const uint8_t c1[6] = {0xC1, 0x02, 0x03, 0x04, 0x05, 0x06};
    const uint8_t c2[6] = {0xC2, 0x02, 0x03, 0x04, 0x05, 0x06};

    EXPECT_CALL(*suite->device.mockPeerComms, addEspNowPeer(_)).Times(testing::AnyNumber());
    int membershipChanges = 0;
    suite->rdc.setOnMembershipChange([&]() { membershipChanges++; });

    connectJack(suite, suite->outJack, SerialIdentifier::OUTPUT_JACK, suite->helloFrame(0xB1));
    ASSERT_EQ(suite->rdc.getChainRole(), ChainRole::HEAD);

    std::vector<uint8_t> a1 = announceBytes(suite->localMac, 1);
    std::vector<uint8_t> a2 = announceBytes(c1, 1);
    suite->transport()->deliverIncoming(PktType::kConnectionAnnounce, c1, a1.data(), a1.size());
    suite->transport()->deliverIncoming(PktType::kConnectionAnnounce, c2, a2.data(), a2.size());
    ASSERT_EQ(suite->rdc.getChainMembers().size(), 2u);
    ASSERT_EQ(membershipChanges, 2);

    std::vector<uint8_t> selfReport = disconnectReportBytes(suite->localMac, 1);
    suite->transport()->deliverIncoming(PktType::kDisconnectReport, c1,
                                        selfReport.data(), selfReport.size());
    EXPECT_EQ(suite->rdc.getChainMembers().size(), 2u);
    EXPECT_EQ(membershipChanges, 2);
}

// A member's own post-merge announce names its true upstream; a HeadTransfer
// arriving later (reliable send, subject to backoff) must not flatten that
// edge onto the demoted head, or the member gets pruned with the wrong subtree.
inline void rdcHeadTransferDoesNotOverwriteAnnouncedUpstream(RDCHelloTests* suite) {
    const uint8_t oldHead[6] = {0x77, 0x02, 0x03, 0x04, 0x05, 0x06};
    const uint8_t member[6] = {0xA1, 0x02, 0x03, 0x04, 0x05, 0x06};
    const uint8_t b1[6] = {0xB1, 0x02, 0x03, 0x04, 0x05, 0x06};

    EXPECT_CALL(*suite->device.mockPeerComms, addEspNowPeer(_)).Times(testing::AnyNumber());

    connectJack(suite, suite->outJack, SerialIdentifier::OUTPUT_JACK, suite->helloFrame(0xB1));
    ASSERT_EQ(suite->rdc.getChainRole(), ChainRole::HEAD);

    // The member's announce lands first, recording its true upstream (b1).
    std::vector<uint8_t> a1 = announceBytes(b1, 1);
    suite->transport()->deliverIncoming(PktType::kConnectionAnnounce, member, a1.data(), a1.size());
    ASSERT_EQ(suite->rdc.getChainMembers().size(), 1u);

    // The demoted head's transfer arrives late, listing the same member under
    // its stale pre-merge upstream (the demoted head itself).
    std::array<uint8_t, 6> m{};
    std::array<uint8_t, 6> staleUpstream{};
    memcpy(m.data(), member, 6);
    memcpy(staleUpstream.data(), oldHead, 6);
    std::vector<uint8_t> handoff = headTransferBytes({{m, staleUpstream}}, 5);
    suite->transport()->deliverIncoming(PktType::kHeadTransfer, oldHead,
                                        handoff.data(), handoff.size());

    // The demoted head departing must not take the member with it: the member's
    // recorded upstream is still b1.
    std::vector<uint8_t> report = disconnectReportBytes(oldHead, 1);
    suite->transport()->deliverIncoming(PktType::kDisconnectReport, b1,
                                        report.data(), report.size());
    std::vector<std::array<uint8_t, 6>> members = suite->rdc.getChainMembers();
    ASSERT_EQ(members.size(), 1u);
    EXPECT_EQ(0, memcmp(members[0].data(), member, 6));
}

// Head propagation is hop-by-hop, so a report can be addressed to a device
// that was just demoted; it forwards the edit to the head it now holds
// instead of dropping it.
inline void rdcDemotedDeviceForwardsDisconnectReport(RDCHelloTests* suite) {
    const uint8_t upstream[6] = {0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F};
    const uint8_t head[6] = {0xF0, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5};
    const uint8_t lost[6] = {0xD1, 0x02, 0x03, 0x04, 0x05, 0x06};
    const uint8_t reporter[6] = {0xC1, 0x02, 0x03, 0x04, 0x05, 0x06};

    EXPECT_CALL(*suite->device.mockPeerComms, addEspNowPeer(_)).Times(testing::AnyNumber());
    RosterSendCapture forwarded;
    captureSends(suite, PktType::kDisconnectReport, forwarded);

    connectJack(suite, suite->inJack, SerialIdentifier::INPUT_JACK,
                chainHelloFrame(upstream, head));
    ASSERT_EQ(suite->rdc.getChainRole(), ChainRole::CHILD);

    std::vector<uint8_t> report = disconnectReportBytes(lost, 1);
    suite->transport()->deliverIncoming(PktType::kDisconnectReport, reporter,
                                        report.data(), report.size());
    ASSERT_EQ(forwarded.count, 1);
    EXPECT_EQ(0, memcmp(forwarded.lastDst.data(), head, 6));
    DisconnectReportPayload sent{};
    ASSERT_EQ(forwarded.lastPayload.size(), sizeof(sent));
    memcpy(&sent, forwarded.lastPayload.data(), sizeof(sent));
    EXPECT_EQ(0, memcmp(sent.disconnectedMac, lost, 6));
}

// An ex-head is STANDALONE, not a roster authority: late Resender retries from
// its dead chain must not seed entries that resurface when it heads again.
inline void rdcStandaloneIgnoresLateRosterTraffic(RDCHelloTests* suite) {
    const uint8_t m1[6] = {0xA1, 0x02, 0x03, 0x04, 0x05, 0x06};
    const uint8_t m2[6] = {0xA2, 0x02, 0x03, 0x04, 0x05, 0x06};
    const uint8_t oldHead[6] = {0x77, 0x02, 0x03, 0x04, 0x05, 0x06};

    EXPECT_CALL(*suite->device.mockPeerComms, addEspNowPeer(_)).Times(testing::AnyNumber());
    int membershipChanges = 0;
    suite->rdc.setOnMembershipChange([&]() { membershipChanges++; });
    ASSERT_EQ(suite->rdc.getChainRole(), ChainRole::STANDALONE);

    std::vector<uint8_t> late = announceBytes(oldHead, 3);
    suite->transport()->deliverIncoming(PktType::kConnectionAnnounce, m1, late.data(), late.size());
    std::array<uint8_t, 6> m{};
    std::array<uint8_t, 6> mUpstream{};
    memcpy(m.data(), m2, 6);
    memcpy(mUpstream.data(), oldHead, 6);
    std::vector<uint8_t> handoff = headTransferBytes({{m, mUpstream}}, 4);
    suite->transport()->deliverIncoming(PktType::kHeadTransfer, oldHead,
                                        handoff.data(), handoff.size());
    EXPECT_EQ(membershipChanges, 0);

    // Heading a fresh chain later must start from an empty roster.
    connectJack(suite, suite->outJack, SerialIdentifier::OUTPUT_JACK, suite->helloFrame(0xB1));
    ASSERT_EQ(suite->rdc.getChainRole(), ChainRole::HEAD);
    EXPECT_TRUE(suite->rdc.getChainMembers().empty());
}

// A head losing its one OUTPUT link loses the entire chain, including members
// whose recorded upstream path went gappy — a subtree walk from the direct
// child alone would leave those behind as phantoms for the next chain.
inline void rdcHeadDirectChildLossClearsWholeRoster(RDCHelloTests* suite) {
    const uint8_t b1[6] = {0xB1, 0x02, 0x03, 0x04, 0x05, 0x06};
    const uint8_t gapped[6] = {0xD7, 0x02, 0x03, 0x04, 0x05, 0x06};
    const uint8_t unknownUpstream[6] = {0xEE, 0x02, 0x03, 0x04, 0x05, 0x06};

    EXPECT_CALL(*suite->device.mockPeerComms, addEspNowPeer(_)).Times(testing::AnyNumber());
    EXPECT_CALL(*suite->device.mockPeerComms, removeEspNowPeer(_)).Times(testing::AnyNumber());

    connectJack(suite, suite->outJack, SerialIdentifier::OUTPUT_JACK, suite->helloFrame(0xB1));
    ASSERT_EQ(suite->rdc.getChainRole(), ChainRole::HEAD);

    std::vector<uint8_t> a1 = announceBytes(suite->localMac, 1);
    std::vector<uint8_t> a2 = announceBytes(unknownUpstream, 1);
    suite->transport()->deliverIncoming(PktType::kConnectionAnnounce, b1, a1.data(), a1.size());
    suite->transport()->deliverIncoming(PktType::kConnectionAnnounce, gapped, a2.data(), a2.size());
    ASSERT_EQ(suite->rdc.getChainMembers().size(), 2u);

    // The direct child's cable dies.
    suite->fakeClock->advance(RemoteDeviceCoordinator::HELLO_SILENT_LINK_MS + 1);
    suite->rdc.sync(&suite->device);
    ASSERT_EQ(suite->rdc.getHelloLinkState(SerialIdentifier::OUTPUT_JACK),
              RemoteDeviceCoordinator::HelloLinkState::IDLE);

    // Re-heading a chain must not resurface the gapped member.
    connectJack(suite, suite->outJack, SerialIdentifier::OUTPUT_JACK, suite->helloFrame(0xB1));
    ASSERT_EQ(suite->rdc.getChainRole(), ChainRole::HEAD);
    EXPECT_TRUE(suite->rdc.getChainMembers().empty());
}

// A late SEND_SUCCESS for a superseded announce (head flapped away and back,
// so destination alone can't tell them apart) must not raise confirmed: only
// the most recently sent announce's delivery counts.
inline void rdcStaleAnnounceDeliveryDoesNotConfirm(RDCHelloTests* suite) {
    const uint8_t upstream[6] = {0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F};
    const uint8_t h1[6] = {0xF0, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5};
    const uint8_t h2[6] = {0xE0, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5};

    EXPECT_CALL(*suite->device.mockPeerComms, addEspNowPeer(_)).Times(testing::AnyNumber());
    EXPECT_CALL(*suite->device.mockPeerComms, removeEspNowPeer(_)).Times(testing::AnyNumber());
    RosterSendCapture announce;
    captureSends(suite, PktType::kConnectionAnnounce, announce);

    suite->deliverHello(suite->inJack, chainHelloFrame(upstream, h1));
    suite->rdc.sync(&suite->device);
    std::vector<uint8_t> ctx = pdnContextBytes(/*chainRole=*/1, /*userId=*/7, /*seqId=*/3);
    suite->transport()->deliverIncoming(
        PktType::kPdnConnectionContext, upstream, ctx.data(), ctx.size());
    suite->rdc.sync(&suite->device);
    ASSERT_EQ(announce.count, 1);
    std::vector<uint8_t> staleAnnounce = announce.lastPayload;

    // Head flaps to h2 and back to h1: two more announces supersede the first.
    suite->deliverHello(suite->inJack, chainHelloFrame(upstream, h2));
    suite->deliverHello(suite->inJack, chainHelloFrame(upstream, h1));
    ASSERT_EQ(announce.count, 3);

    // The first announce's SEND_SUCCESS straggles in: same destination, stale seqId.
    suite->transport()->onSendResult(PktType::kConnectionAnnounce, h1,
                                     staleAnnounce.data(), staleAnnounce.size(), true);
    suite->outJack.clearOutput();
    suite->rdc.emitHello();
    EXPECT_EQ(parseEmittedHello(suite->outJack.getOutput()).confirmed, 0);

    suite->transport()->onSendResult(PktType::kConnectionAnnounce, h1,
                                     announce.lastPayload.data(),
                                     announce.lastPayload.size(), true);
    suite->outJack.clearOutput();
    suite->rdc.emitHello();
    EXPECT_EQ(parseEmittedHello(suite->outJack.getOutput()).confirmed, 1);
}

// The held head is a unicast target that is usually not an adjacent HELLO
// peer, so head adoption must claim its radio slot and a head change or link
// loss must release it — without touching a head that is also the jack peer.
inline void rdcHeadAdoptionManagesRadioSlot(RDCHelloTests* suite) {
    const uint8_t peerIsHead[6] = {0xF0, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5};
    const uint8_t newHead[6] = {0xE0, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5};

    std::vector<std::array<uint8_t, 6>> added;
    std::vector<std::array<uint8_t, 6>> removed;
    ON_CALL(*suite->device.mockPeerComms, addEspNowPeer(_))
        .WillByDefault(testing::DoAll(
            testing::Invoke([&added](const uint8_t* mac) {
                added.emplace_back();
                memcpy(added.back().data(), mac, 6);
            }),
            Return(0)));
    ON_CALL(*suite->device.mockPeerComms, removeEspNowPeer(_))
        .WillByDefault(testing::DoAll(
            testing::Invoke([&removed](const uint8_t* mac) {
                removed.emplace_back();
                memcpy(removed.back().data(), mac, 6);
            }),
            Return(0)));
    auto contains = [](const std::vector<std::array<uint8_t, 6>>& macs, const uint8_t* mac) {
        for (const std::array<uint8_t, 6>& m : macs)
            if (memcmp(m.data(), mac, 6) == 0) return true;
        return false;
    };

    // The INPUT peer is itself the head: adjacent, slot claimed by the link.
    connectJack(suite, suite->inJack, SerialIdentifier::INPUT_JACK,
                chainHelloFrame(peerIsHead, nullptr));
    EXPECT_TRUE(contains(added, peerIsHead));

    // The upstream re-advertises a farther head: claim it, keep the jack peer.
    suite->deliverHello(suite->inJack, chainHelloFrame(peerIsHead, newHead));
    EXPECT_TRUE(contains(added, newHead));
    EXPECT_FALSE(contains(removed, peerIsHead));

    // The link dies: the jack peer's slot and the held head's slot both go.
    suite->fakeClock->advance(RemoteDeviceCoordinator::HELLO_SILENT_LINK_MS + 1);
    suite->rdc.sync(&suite->device);
    EXPECT_TRUE(contains(removed, peerIsHead));
    EXPECT_TRUE(contains(removed, newHead));
}

// The held head keeping its radio slot must not keep the dead jack's context
// retries: with the INPUT peer itself the held head, link death keeps the slot
// in releaseHelloPeer, then onLinkLost unregisters it — a surviving context
// retry would re-register the slot inside the driver and leak it permanently.
inline void rdcInputHeadLinkDeathCancelsPendingContextSend(RDCHelloTests* suite) {
    const uint8_t peer[6] = {0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F};

    EXPECT_CALL(*suite->device.mockPeerComms, addEspNowPeer(_)).Times(testing::AnyNumber());
    EXPECT_CALL(*suite->device.mockPeerComms, removeEspNowPeer(_)).Times(testing::AnyNumber());

    // The INPUT peer advertises no head, so it IS the head this device adopts.
    suite->deliverHello(suite->inJack, chainHelloFrame(peer, nullptr));
    suite->rdc.sync(&suite->device);
    ASSERT_EQ(suite->rdc.getHelloLinkState(SerialIdentifier::INPUT_JACK),
              RemoteDeviceCoordinator::HelloLinkState::CONNECTING);
    ASSERT_TRUE(suite->rdc.isContextSendPending(peer));

    // The exchange never completes (no SEND_SUCCESS); the link dies.
    suite->fakeClock->advance(RemoteDeviceCoordinator::CONTEXT_EXCHANGE_TIMEOUT_MS + 1);
    suite->rdc.sync(&suite->device);
    ASSERT_EQ(suite->rdc.getHelloLinkState(SerialIdentifier::INPUT_JACK),
              RemoteDeviceCoordinator::HelloLinkState::IDLE);

    EXPECT_FALSE(suite->rdc.isContextSendPending(peer));
}

// Reports are distinct events, not current state: two reports forwarded to the
// same head back to back (routine when a demoted device relays two children's
// reports in one tick) must EACH keep a retry slot, so both retransmit while
// undelivered. A per-target supersede would erase the first report's slot.
inline void rdcBackToBackReportsBothRetryToHead(RDCHelloTests* suite) {
    const uint8_t upstream[6] = {0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F};
    const uint8_t head[6] = {0xF0, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5};
    const uint8_t lost1[6] = {0xD1, 0x02, 0x03, 0x04, 0x05, 0x06};
    const uint8_t lost2[6] = {0xD2, 0x02, 0x03, 0x04, 0x05, 0x06};
    const uint8_t reporter1[6] = {0xC1, 0x02, 0x03, 0x04, 0x05, 0x06};
    const uint8_t reporter2[6] = {0xC2, 0x02, 0x03, 0x04, 0x05, 0x06};

    EXPECT_CALL(*suite->device.mockPeerComms, addEspNowPeer(_)).Times(testing::AnyNumber());
    RosterSendCapture forwarded;
    captureSends(suite, PktType::kDisconnectReport, forwarded);

    connectJack(suite, suite->inJack, SerialIdentifier::INPUT_JACK,
                chainHelloFrame(upstream, head));
    ASSERT_EQ(suite->rdc.getChainRole(), ChainRole::CHILD);

    std::vector<uint8_t> r1 = disconnectReportBytes(lost1, 1);
    std::vector<uint8_t> r2 = disconnectReportBytes(lost2, 1);
    suite->transport()->deliverIncoming(PktType::kDisconnectReport, reporter1,
                                        r1.data(), r1.size());
    suite->transport()->deliverIncoming(PktType::kDisconnectReport, reporter2,
                                        r2.data(), r2.size());
    ASSERT_EQ(forwarded.count, 2);

    // Neither delivery is confirmed; the next backoff tick must retransmit BOTH.
    // Keep the upstream link alive across the advance: an INPUT silent-link
    // would clear the head and cancel the very retries under test.
    suite->fakeClock->advance(Resender::INITIAL_TIMEOUT_MS + 1);
    suite->deliverHello(suite->inJack, chainHelloFrame(upstream, head));
    suite->rdc.sync(&suite->device);
    EXPECT_EQ(forwarded.count, 4);
}

// A head change cancels the in-flight report to the old head (releaseHeadPeer),
// and nothing else re-sends it — unlike the announce path, which re-announces.
// The pending report must follow the head: re-sent to the successor, and, once
// delivered, never re-sent again on a later head change.
inline void rdcPendingReportResentOnHeadChange(RDCHelloTests* suite) {
    const uint8_t upstream[6] = {0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F};
    const uint8_t h1[6] = {0xF0, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5};
    const uint8_t h2[6] = {0xE0, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5};
    const uint8_t h3[6] = {0xD0, 0xD1, 0xD2, 0xD3, 0xD4, 0xD5};
    const uint8_t downstream[6] = {0xD1, 0x02, 0x03, 0x04, 0x05, 0x06};

    EXPECT_CALL(*suite->device.mockPeerComms, addEspNowPeer(_)).Times(testing::AnyNumber());
    EXPECT_CALL(*suite->device.mockPeerComms, removeEspNowPeer(_)).Times(testing::AnyNumber());
    RosterSendCapture report;
    captureSends(suite, PktType::kDisconnectReport, report);

    connectJack(suite, suite->inJack, SerialIdentifier::INPUT_JACK,
                chainHelloFrame(upstream, h1));
    connectJack(suite, suite->outJack, SerialIdentifier::OUTPUT_JACK,
                suite->helloFrame(0xD1));
    ASSERT_EQ(suite->rdc.getChainRole(), ChainRole::CHILD);

    // Downstream dies: report goes to h1, but its delivery is never confirmed.
    suite->fakeClock->advance(RemoteDeviceCoordinator::HELLO_SILENT_LINK_MS + 1);
    suite->deliverHello(suite->inJack, chainHelloFrame(upstream, h1));
    suite->rdc.sync(&suite->device);
    ASSERT_EQ(report.count, 1);
    ASSERT_EQ(0, memcmp(report.lastDst.data(), h1, 6));

    // Head changes to h2: the undelivered report must chase the successor.
    suite->deliverHello(suite->inJack, chainHelloFrame(upstream, h2));
    ASSERT_EQ(report.count, 2);
    EXPECT_EQ(0, memcmp(report.lastDst.data(), h2, 6));
    DisconnectReportPayload resent{};
    ASSERT_EQ(report.lastPayload.size(), sizeof(resent));
    memcpy(&resent, report.lastPayload.data(), sizeof(resent));
    EXPECT_EQ(0, memcmp(resent.disconnectedMac, downstream, 6));

    // Delivery lands; a later head change must NOT re-send the settled report.
    suite->transport()->onSendResult(PktType::kDisconnectReport, h2,
                                     report.lastPayload.data(),
                                     report.lastPayload.size(), true);
    suite->deliverHello(suite->inJack, chainHelloFrame(upstream, h3));
    EXPECT_EQ(report.count, 2);
}

// A downstream-loss report is owed only while a child under a head. If the child
// then loses its own INPUT link it becomes its own head, voiding the report;
// adopting a fresh head later must NOT re-send the stale report to it (which
// would prune a live member whose MAC got reused under the new head).
inline void rdcPendingReportVoidedByBecomingHead(RDCHelloTests* suite) {
    const uint8_t upstream[6] = {0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F};
    const uint8_t h1[6] = {0xF0, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5};
    const uint8_t upstream2[6] = {0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F};
    const uint8_t h2[6] = {0xE0, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5};
    const uint8_t downstream[6] = {0xD1, 0x02, 0x03, 0x04, 0x05, 0x06};

    EXPECT_CALL(*suite->device.mockPeerComms, addEspNowPeer(_)).Times(testing::AnyNumber());
    EXPECT_CALL(*suite->device.mockPeerComms, removeEspNowPeer(_)).Times(testing::AnyNumber());
    RosterSendCapture report;
    captureSends(suite, PktType::kDisconnectReport, report);

    connectJack(suite, suite->inJack, SerialIdentifier::INPUT_JACK,
                chainHelloFrame(upstream, h1));
    connectJack(suite, suite->outJack, SerialIdentifier::OUTPUT_JACK,
                suite->helloFrame(0xD1));
    ASSERT_EQ(suite->rdc.getChainRole(), ChainRole::CHILD);

    // Downstream dies: report to h1, delivery never confirmed, so the report
    // stays pending. Keep the INPUT link alive so only OUTPUT goes silent.
    suite->fakeClock->advance(RemoteDeviceCoordinator::HELLO_SILENT_LINK_MS + 1);
    suite->deliverHello(suite->inJack, chainHelloFrame(upstream, h1));
    suite->rdc.sync(&suite->device);
    ASSERT_EQ(report.count, 1);
    ASSERT_EQ(0, memcmp(report.lastDst.data(), h1, 6));

    // INPUT link drops: this device becomes its own head, voiding the report.
    // releaseHeadPeer cancels the h1 retries here, so the count settles; baseline
    // it (a resender retransmit to h1 may have fired) to isolate the h2 re-send.
    suite->fakeClock->advance(RemoteDeviceCoordinator::HELLO_SILENT_LINK_MS + 1);
    suite->rdc.sync(&suite->device);
    ASSERT_EQ(suite->rdc.getChainRole(), ChainRole::STANDALONE);
    const int baseline = report.count;

    // Adopt a fresh head h2 via a new upstream: the voided report must not chase it.
    suite->deliverHello(suite->inJack, chainHelloFrame(upstream2, h2));
    EXPECT_EQ(report.count, baseline);
    EXPECT_NE(0, memcmp(report.lastDst.data(), h2, 6));
}

// During a head transient two mutually-stale non-authorities can each hold the
// other as head; forwarding a report back to the device it arrived from would
// bounce it between them, each hop accumulating a fresh pending retry entry.
inline void rdcReportFromHeldHeadNotForwardedBack(RDCHelloTests* suite) {
    const uint8_t head[6] = {0xF0, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5};
    const uint8_t lost[6] = {0xD1, 0x02, 0x03, 0x04, 0x05, 0x06};

    EXPECT_CALL(*suite->device.mockPeerComms, addEspNowPeer(_)).Times(testing::AnyNumber());
    RosterSendCapture forwarded;
    captureSends(suite, PktType::kDisconnectReport, forwarded);

    // The INPUT peer advertises no head, so it IS the head this device holds.
    connectJack(suite, suite->inJack, SerialIdentifier::INPUT_JACK,
                chainHelloFrame(head, nullptr));
    ASSERT_EQ(suite->rdc.getChainRole(), ChainRole::CHILD);

    std::vector<uint8_t> report = disconnectReportBytes(lost, 1);
    suite->transport()->deliverIncoming(PktType::kDisconnectReport, head,
                                        report.data(), report.size());
    EXPECT_EQ(forwarded.count, 0);
}

// Receiving a transfer records each member under its TRUE upstream from the
// payload, so a mid-chain transferred member departing prunes exactly its
// downstream tail — not the whole transferred set, and not just itself.
inline void rdcHeadTransferReceiveMergesAndPrunes(RDCHelloTests* suite) {
    const uint8_t oldHead[6] = {0x77, 0x02, 0x03, 0x04, 0x05, 0x06};
    const uint8_t memberA[6] = {0xA1, 0x02, 0x03, 0x04, 0x05, 0x06};
    const uint8_t memberB[6] = {0xB2, 0x02, 0x03, 0x04, 0x05, 0x06};
    const uint8_t memberC[6] = {0xC3, 0x02, 0x03, 0x04, 0x05, 0x06};

    EXPECT_CALL(*suite->device.mockPeerComms, addEspNowPeer(_)).Times(testing::AnyNumber());
    int membershipChanges = 0;
    suite->rdc.setOnMembershipChange([&]() { membershipChanges++; });

    connectJack(suite, suite->outJack, SerialIdentifier::OUTPUT_JACK, suite->helloFrame(0xB1));
    ASSERT_EQ(suite->rdc.getChainRole(), ChainRole::HEAD);

    // Transferred chain: oldHead <- A <- B <- C, each pair naming the true edge.
    std::array<uint8_t, 6> old{};
    std::array<uint8_t, 6> a{};
    std::array<uint8_t, 6> b{};
    std::array<uint8_t, 6> c{};
    memcpy(old.data(), oldHead, 6);
    memcpy(a.data(), memberA, 6);
    memcpy(b.data(), memberB, 6);
    memcpy(c.data(), memberC, 6);
    std::vector<uint8_t> handoff = headTransferBytes({{a, old}, {b, a}, {c, b}}, 5);
    suite->transport()->deliverIncoming(PktType::kHeadTransfer, oldHead,
                                        handoff.data(), handoff.size());
    EXPECT_EQ(membershipChanges, 1);
    EXPECT_EQ(suite->rdc.getChainMembers().size(), 3u);

    // Mid-chain B departs: C hangs off B and goes too; A stays.
    std::vector<uint8_t> report = disconnectReportBytes(memberB, 1);
    suite->transport()->deliverIncoming(PktType::kDisconnectReport, memberA,
                                        report.data(), report.size());
    EXPECT_EQ(membershipChanges, 2);
    std::vector<std::array<uint8_t, 6>> members = suite->rdc.getChainMembers();
    ASSERT_EQ(members.size(), 1u);
    EXPECT_EQ(0, memcmp(members[0].data(), memberA, 6));

    // The demoted head departs: the rest of its transferred subtree follows.
    std::vector<uint8_t> headLoss = disconnectReportBytes(oldHead, 2);
    suite->transport()->deliverIncoming(PktType::kDisconnectReport, memberA,
                                        headLoss.data(), headLoss.size());
    EXPECT_EQ(membershipChanges, 3);
    EXPECT_TRUE(suite->rdc.getChainMembers().empty());
}

// One OUTPUT jack means one downstream: a fresh announce claiming an upstream
// another member already recorded evicts the stale claimant and its subtree.
inline void rdcAnnounceEvictsStaleUpstreamClaimant(RDCHelloTests* suite) {
    const uint8_t b1[6] = {0xB1, 0x02, 0x03, 0x04, 0x05, 0x06};
    const uint8_t memberC[6] = {0xC1, 0x02, 0x03, 0x04, 0x05, 0x06};
    const uint8_t memberD[6] = {0xD1, 0x02, 0x03, 0x04, 0x05, 0x06};
    const uint8_t memberE[6] = {0xE1, 0x02, 0x03, 0x04, 0x05, 0x06};

    EXPECT_CALL(*suite->device.mockPeerComms, addEspNowPeer(_)).Times(testing::AnyNumber());

    connectJack(suite, suite->outJack, SerialIdentifier::OUTPUT_JACK, suite->helloFrame(0xB1));
    ASSERT_EQ(suite->rdc.getChainRole(), ChainRole::HEAD);

    // Roster: C hangs off b1, E hangs off C.
    std::vector<uint8_t> a1 = announceBytes(b1, 1);
    std::vector<uint8_t> a2 = announceBytes(memberC, 1);
    suite->transport()->deliverIncoming(PktType::kConnectionAnnounce, memberC, a1.data(), a1.size());
    suite->transport()->deliverIncoming(PktType::kConnectionAnnounce, memberE, a2.data(), a2.size());
    ASSERT_EQ(suite->rdc.getChainMembers().size(), 2u);

    // D now claims b1: C was unplugged (its report may still be in flight), so
    // C and its subtree leave and D is the sole downstream of b1.
    std::vector<uint8_t> a3 = announceBytes(b1, 1);
    suite->transport()->deliverIncoming(PktType::kConnectionAnnounce, memberD, a3.data(), a3.size());
    std::vector<std::array<uint8_t, 6>> members = suite->rdc.getChainMembers();
    ASSERT_EQ(members.size(), 1u);
    EXPECT_EQ(0, memcmp(members[0].data(), memberD, 6));
}

// A resend of the SAME member's announce (fresh seqId, same content) is not a
// competing claim and must evict nobody.
inline void rdcDuplicateReannounceDoesNotEvict(RDCHelloTests* suite) {
    const uint8_t b1[6] = {0xB1, 0x02, 0x03, 0x04, 0x05, 0x06};
    const uint8_t memberC[6] = {0xC1, 0x02, 0x03, 0x04, 0x05, 0x06};
    const uint8_t memberE[6] = {0xE1, 0x02, 0x03, 0x04, 0x05, 0x06};

    EXPECT_CALL(*suite->device.mockPeerComms, addEspNowPeer(_)).Times(testing::AnyNumber());
    int membershipChanges = 0;
    suite->rdc.setOnMembershipChange([&]() { membershipChanges++; });

    connectJack(suite, suite->outJack, SerialIdentifier::OUTPUT_JACK, suite->helloFrame(0xB1));
    ASSERT_EQ(suite->rdc.getChainRole(), ChainRole::HEAD);

    std::vector<uint8_t> a1 = announceBytes(b1, 1);
    std::vector<uint8_t> a2 = announceBytes(memberC, 1);
    suite->transport()->deliverIncoming(PktType::kConnectionAnnounce, memberC, a1.data(), a1.size());
    suite->transport()->deliverIncoming(PktType::kConnectionAnnounce, memberE, a2.data(), a2.size());
    ASSERT_EQ(membershipChanges, 2);

    std::vector<uint8_t> dup = announceBytes(b1, 2);
    suite->transport()->deliverIncoming(PktType::kConnectionAnnounce, memberC, dup.data(), dup.size());
    EXPECT_EQ(membershipChanges, 2);
    EXPECT_EQ(suite->rdc.getChainMembers().size(), 2u);
}

// A stale head-transfer snapshot must not re-fork an upstream a fresher announce
// already claimed: Y announces Y->b1, then a demoted head's older transfer lists
// X->b1 (X absent). The transfer is skipped, so b1 keeps its one downstream and
// the roster stays a list. Without the upstreamHeldByOther skip, X inserts and
// b1 forks.
inline void rdcStaleTransferDoesNotReforkClaimedUpstream(RDCHelloTests* suite) {
    const uint8_t b1[6] = {0xB1, 0x02, 0x03, 0x04, 0x05, 0x06};
    const uint8_t memberY[6] = {0xE1, 0x02, 0x03, 0x04, 0x05, 0x06};
    const uint8_t memberX[6] = {0xA1, 0x02, 0x03, 0x04, 0x05, 0x06};
    const uint8_t oldHead[6] = {0x77, 0x02, 0x03, 0x04, 0x05, 0x06};

    EXPECT_CALL(*suite->device.mockPeerComms, addEspNowPeer(_)).Times(testing::AnyNumber());

    connectJack(suite, suite->outJack, SerialIdentifier::OUTPUT_JACK, suite->helloFrame(0xB1));
    ASSERT_EQ(suite->rdc.getChainRole(), ChainRole::HEAD);

    // Y is the current, fresher claimant of b1.
    std::vector<uint8_t> ay = announceBytes(b1, 1);
    suite->transport()->deliverIncoming(PktType::kConnectionAnnounce, memberY, ay.data(), ay.size());
    ASSERT_EQ(suite->rdc.getChainMembers().size(), 1u);

    // A stale transfer arrives listing X on the same upstream b1.
    std::array<uint8_t, 6> x{};
    std::array<uint8_t, 6> up{};
    memcpy(x.data(), memberX, 6);
    memcpy(up.data(), b1, 6);
    std::vector<uint8_t> handoff = headTransferBytes({{x, up}}, 5);
    suite->transport()->deliverIncoming(PktType::kHeadTransfer, oldHead,
                                        handoff.data(), handoff.size());

    // b1 still has exactly one downstream, and it is Y not X.
    std::vector<std::array<uint8_t, 6>> members = suite->rdc.getChainMembers();
    ASSERT_EQ(members.size(), 1u);
    EXPECT_EQ(0, memcmp(members[0].data(), memberY, 6));
}
