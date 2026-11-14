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
#include <functional>
#include <utility>
#include <vector>

// Radio mock defaults: sends succeed, the radio reads ENABLED, the device reports `mac` as its own.
inline void wireRadioDefaults(MockDevice& device, uint8_t* mac) {
    ON_CALL(*device.mockPeerComms, sendData(testing::_, testing::_, testing::_, testing::_))
    .WillByDefault(testing::Return(1));
    ON_CALL(*device.mockPeerComms, getMacAddress()).WillByDefault(testing::Return(mac));
    ON_CALL(*device.mockPeerComms, getPeerCommsState())
    .WillByDefault(testing::Return(PeerCommsState::ENABLED));
}

// Pushes a frame into a jack's RX and drains it via the real exec() pump.
inline void deliverFrame(NativeSerialDriver& jack, const std::vector<uint8_t>& frame) {
    jack.injectBytes(frame);
    jack.exec();
}

// The wire bytes of a peer's PdnConnectionContext.
inline std::vector<uint8_t> pdnContextBytes(ChainRole chainRole, uint16_t userId, uint8_t seqId,
                                            uint8_t gameRole = 0) {
    PdnConnectionContext ctx{};
    ctx.seqId = seqId;
    ctx.chainRole = chainRole;
    ctx.player.userId = userId;
    ctx.player.gameRole = gameRole;
    std::vector<uint8_t> bytes(sizeof(ctx));
    memcpy(bytes.data(), &ctx, sizeof(ctx));
    return bytes;
}

// A framed HELLO carrying a source MAC and, optionally, an advertised head
// and flag bits.
inline std::vector<uint8_t> chainHelloFrame(const uint8_t* source, const uint8_t* head,
                                            uint8_t flags = 0) {
    HelloPayload hello{};
    memcpy(hello.source, source, 6);
    hello.deviceType = static_cast<uint8_t>(DeviceType::PDN);
    if (head != nullptr) memcpy(hello.headMac, head, 6);
    hello.flags = flags;
    return encodeFramed(hello);
}

inline std::vector<uint8_t> announceBytes(const uint8_t* upstream, uint8_t seqId) {
    ConnectionAnnouncePayload payload{};
    payload.seqId = seqId;
    memcpy(payload.upstreamMac, upstream, 6);
    std::vector<uint8_t> bytes(sizeof(payload));
    memcpy(bytes.data(), &payload, sizeof(payload));
    return bytes;
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

// A native jack that records every transmitted byte and runs a hook after each,
// standing in for the main loop running mid-frame on the ESP32.
class TappedSerialDriver : public NativeSerialDriver {
public:
    /** Names the jack as NativeSerialDriver does. */
    explicit TappedSerialDriver(const std::string& name)
        : NativeSerialDriver(name) {}

    /** Transmits the byte, records it, then runs the hook with the count so far. */
    void print(char msg) override {
        NativeSerialDriver::print(msg);
        wire.push_back(static_cast<uint8_t>(msg));
        if (afterByte) afterByte(wire.size());
    }

    std::vector<uint8_t> wire;
    std::function<void(size_t)> afterByte;
};

// One device on a real RDC with its own jacks, for tests that cable live RDCs
// to each other. The emit task is externalized so the test is the HELLO writer.
struct CabledRdcNode {
    /** Wires the node's RDC to its own jacks under the given MAC. */
    explicit CabledRdcNode(const uint8_t nodeMac[6]) {
        memcpy(mac, nodeMac, 6);
        wireRadioDefaults(device, mac);
        device.serialManager->setOutputJack(&out);
        device.serialManager->setInputJack(&in);
        rdc.setExternalConnectivityTask(true);
        rdc.initialize(device.wirelessManager, device.serialManager, &device);
        rdc.setOnRingClosed([this]() { ringClosedCount++; }, this);
    }

    MockDevice device;
    TappedSerialDriver out{"cabled-out"};
    TappedSerialDriver in{"cabled-in"};
    RemoteDeviceCoordinator rdc;
    uint8_t mac[6] = {};
    int ringClosedCount = 0;
};

// Carries what each jack has written across a set of plugged cables, each
// written (upper, lower) meaning upper's OUTPUT into lower's INPUT, then lets
// every node's links catch up. Unpumped jack output is dropped — an unplugged
// jack transmits into open air. These nodes share no radio, so the ESP-NOW
// context exchange is stood in for the moment a jack starts one.
inline void deliverCabled(const std::vector<CabledRdcNode*>& nodes,
                          const std::vector<std::pair<size_t, size_t>>& cables) {
    for (const std::pair<size_t, size_t>& cable : cables) {
        pumpCable(nodes[cable.first]->out, nodes[cable.second]->in);
        pumpCable(nodes[cable.second]->in, nodes[cable.first]->out);
    }
    for (CabledRdcNode* n : nodes) {
        n->out.clearOutput();
        n->in.clearOutput();
    }
    for (CabledRdcNode* n : nodes) {
        n->rdc.sync(&n->device);
        for (SerialIdentifier jack :
             {SerialIdentifier::OUTPUT_JACK, SerialIdentifier::INPUT_JACK}) {
            if (n->rdc.getJackStatus(jack) == JackStatus::CONNECTING) {
                n->rdc.onContextExchangeComplete(jack);
            }
        }
        n->rdc.sync(&n->device);
    }
}

// One HELLO cycle: every node's writer runs once, then deliverCabled().
inline void pumpCabledCycle(const std::vector<CabledRdcNode*>& nodes,
                            const std::vector<std::pair<size_t, size_t>>& cables) {
    for (CabledRdcNode* n : nodes)
        n->rdc.emitHello();
    deliverCabled(nodes, cables);
}

// One device on a real RemoteDeviceCoordinator, its emit task externalized so the
// test drives HELLO single-threaded.
class RdcTestHarness : public testing::Test {
public:
    /** Brings the device up on native jacks. */
    void SetUp() override {
        fakeClock = new FakePlatformClock();
        SimpleTimer::setPlatformClock(fakeClock);
        fakeClock->setTime(1000);

        wireRadioDefaults(device, localMac);
        device.serialManager->setOutputJack(&outJack);
        device.serialManager->setInputJack(&inJack);

        rdc.setExternalConnectivityTask(true);
        rdc.initialize(device.wirelessManager, device.serialManager, &device);
    }

    /** Restores the real platform clock. */
    void TearDown() override {
        SimpleTimer::setPlatformClock(nullptr);
        delete fakeClock;
        fakeClock = nullptr;
    }

    /**
     * Feeds a peer's PdnConnectionContext in through the handler the RDC's
     * context channel registered with the radio driver — the production receive path.
     */
    void deliverPdnContext(const uint8_t* peerMac, bool isHunter = false) {
        std::vector<uint8_t> bytes =
        pdnContextBytes(ChainRole::STANDALONE, /*userId=*/4242, ++contextSeqId, isHunter ? 1 : 0);
        ASSERT_TRUE(device.mockPeerComms->triggerPacketCallback(
        peerMac, PktType::kPdnConnectionContext, bytes.data(), bytes.size()))
        << "the RDC registered no context handler";
    }

    /**
     * Brings `jack` from Idle to Connected against `peerMac`, optionally carrying
     * an advertised chain head, the peer's role and HELLO flag bits.
     */
    void connectJackTo(NativeSerialDriver& jack, const uint8_t* peerMac,
                       const uint8_t* advertisedHead = nullptr, bool isHunter = false,
                       uint8_t flags = 0) {
        deliverFrame(jack, chainHelloFrame(peerMac, advertisedHead, flags));
        rdc.sync(&device);
        deliverPdnContext(peerMac, isHunter);
        rdc.sync(&device);
    }

    MockDevice device;
    NativeSerialDriver outJack{"rdc-out"};
    NativeSerialDriver inJack{"rdc-in"};
    // Unregistered unless a fixture hands it to the serial manager before SetUp.
    NativeSerialDriver secondaryJack{"rdc-sec"};
    // Declared after the jacks so it is destroyed first; its dtor clears their
    // byte callbacks, mirroring production where the drivers outlive the RDC.
    RemoteDeviceCoordinator rdc;
    FakePlatformClock* fakeClock = nullptr;
    uint8_t contextSeqId = 0;
    uint8_t localMac[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
};
