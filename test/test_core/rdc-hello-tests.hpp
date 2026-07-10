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

class RDCHelloTests : public testing::Test {
public:
    /// Fake clock, mocked radio, native jacks swapped in, RDC in HELLO mode with
    /// the emit task externalized so the test drives it single-threaded.
    void SetUp() override {
        fakeClock = new FakePlatformClock();
        SimpleTimer::setPlatformClock(fakeClock);
        fakeClock->setTime(1000);

        ON_CALL(*device.mockPeerComms, sendData(_, _, _, _)).WillByDefault(Return(1));
        ON_CALL(*device.mockPeerComms, getMacAddress()).WillByDefault(Return(localMac));
        ON_CALL(*device.mockPeerComms, addEspNowPeer(_)).WillByDefault(Return(0));
        ON_CALL(*device.mockPeerComms, removeEspNowPeer(_)).WillByDefault(Return(0));
        ON_CALL(*device.mockPeerComms, getPeerCommsState())
            .WillByDefault(Return(PeerCommsState::CONNECTED));

        device.serialManager->setOutputJack(&outJack);
        device.serialManager->setInputJack(&inJack);

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
        jack.injectBytes(frame);
        jack.exec();
    }

    MockDevice device;
    FakePlatformClock* fakeClock = nullptr;
    NativeSerialDriver outJack{"hello-out"};
    NativeSerialDriver inJack{"hello-in"};
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

// (e) A new HELLO MAC on the OUT jack sends this device's context (pending
// entry created); the IN jack does not initiate one.
inline void rdcHelloOutputJackInitiatesContext(RDCHelloTests* suite) {
    const uint8_t outPeer[6] = {0xA1, 0x02, 0x03, 0x04, 0x05, 0x06};
    const uint8_t inPeer[6] = {0xB1, 0x02, 0x03, 0x04, 0x05, 0x06};

    suite->deliverHello(suite->outJack, suite->helloFrame(0xA1));
    suite->deliverHello(suite->inJack, suite->helloFrame(0xB1));
    suite->rdc.sync(&suite->device);

    EXPECT_TRUE(suite->rdc.isContextSendPending(outPeer));
    EXPECT_FALSE(suite->rdc.isContextSendPending(inPeer));
    EXPECT_EQ(suite->rdc.getHelloLinkState(SerialIdentifier::INPUT_JACK),
              RemoteDeviceCoordinator::HelloLinkState::CONNECTING);
}

// A PdnConnectionContext carrying the given chainRole + userId, serialized to
// its wire bytes for deliverIncoming.
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

    suite->rdc.getReliableTransport()->onSendResult(
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
    suite->rdc.getReliableTransport()->deliverIncoming(
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

// (f) Output initiates, input replies: a context arriving on an INPUT jack
// completes that jack AND sends our context back, so the initiator's out-jack
// can also complete. Without the reply the initiator loops CONNECTING->IDLE.
inline void rdcContextInputJackReplies(RDCHelloTests* suite) {
    const uint8_t peer[6] = {0xB1, 0x02, 0x03, 0x04, 0x05, 0x06};

    ON_CALL(*suite->device.mockPeerComms, sendData(_, _, _, _))
        .WillByDefault(Return(1));
    EXPECT_CALL(*suite->device.mockPeerComms, addEspNowPeer(_))
        .Times(testing::AnyNumber());

    // Peer arrives on the INPUT jack; an in-jack HELLO must NOT initiate a send.
    suite->deliverHello(suite->inJack, suite->helloFrame(0xB1));
    suite->rdc.sync(&suite->device);  // drive Idle->Connecting (input jack: no send)
    ASSERT_EQ(suite->rdc.getHelloLinkState(SerialIdentifier::INPUT_JACK),
              RemoteDeviceCoordinator::HelloLinkState::CONNECTING);
    ASSERT_FALSE(suite->rdc.isContextSendPending(peer));

    // Receiving the peer's context completes the input jack AND replies.
    std::vector<uint8_t> ctx = pdnContextBytes(/*chainRole=*/1, /*userId=*/7, /*seqId=*/3);
    suite->rdc.getReliableTransport()->deliverIncoming(
        PktType::kPdnConnectionContext, peer, ctx.data(), ctx.size());
    suite->rdc.sync(&suite->device);  // commit Connecting->Connected

    EXPECT_EQ(suite->rdc.getHelloLinkState(SerialIdentifier::INPUT_JACK),
              RemoteDeviceCoordinator::HelloLinkState::CONNECTED);
    EXPECT_TRUE(suite->rdc.isContextSendPending(peer));  // reply was sent
}

// A 2-node ring cables the same peer to BOTH jacks. One received context must
// complete BOTH links; if the un-matched one is left CONNECTING it silent-links to
// IDLE and opens the ring.
inline void rdcContextCompletesBothJacksForSamePeer(RDCHelloTests* suite) {
    const uint8_t peer[6] = {0xB1, 0x02, 0x03, 0x04, 0x05, 0x06};

    ON_CALL(*suite->device.mockPeerComms, sendData(_, _, _, _)).WillByDefault(Return(1));
    EXPECT_CALL(*suite->device.mockPeerComms, addEspNowPeer(_)).Times(testing::AnyNumber());

    suite->deliverHello(suite->outJack, suite->helloFrame(0xB1));
    suite->deliverHello(suite->inJack, suite->helloFrame(0xB1));
    suite->rdc.sync(&suite->device);
    ASSERT_EQ(suite->rdc.getHelloLinkState(SerialIdentifier::OUTPUT_JACK),
              RemoteDeviceCoordinator::HelloLinkState::CONNECTING);
    ASSERT_EQ(suite->rdc.getHelloLinkState(SerialIdentifier::INPUT_JACK),
              RemoteDeviceCoordinator::HelloLinkState::CONNECTING);

    std::vector<uint8_t> ctx = pdnContextBytes(/*chainRole=*/1, /*userId=*/7, /*seqId=*/3);
    suite->rdc.getReliableTransport()->deliverIncoming(
        PktType::kPdnConnectionContext, peer, ctx.data(), ctx.size());
    suite->rdc.sync(&suite->device);

    EXPECT_EQ(suite->rdc.getHelloLinkState(SerialIdentifier::OUTPUT_JACK),
              RemoteDeviceCoordinator::HelloLinkState::CONNECTED);
    EXPECT_EQ(suite->rdc.getHelloLinkState(SerialIdentifier::INPUT_JACK),
              RemoteDeviceCoordinator::HelloLinkState::CONNECTED);
}

// (d) A headMac change mid-exchange cancels the in-flight send and re-initiates.

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
