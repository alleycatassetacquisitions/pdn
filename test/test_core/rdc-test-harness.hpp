#pragma once

#include <cstring>
#include <vector>
#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "device-mock.hpp"
#include "utility-tests.hpp"
#include "device/remote-device-coordinator.hpp"
#include "device/serial-frame-parser.hpp"
#include "device/drivers/native/native-serial-driver.hpp"
#include "protocol-constants.hpp"

/**
 * Installs default radio mock actions shared by RDC and game-manager fixtures:
 * sends succeed, the radio reports `mac` as its own, and the link state reads
 * ENABLED.
 */
inline void wireRadioDefaults(MockDevice& device, uint8_t* mac) {
    ON_CALL(*device.mockPeerComms, sendData(testing::_, testing::_, testing::_, testing::_))
    .WillByDefault(testing::Return(1));
    ON_CALL(*device.mockPeerComms, getMacAddress()).WillByDefault(testing::Return(mac));
    ON_CALL(*device.mockPeerComms, getPeerCommsState())
    .WillByDefault(testing::Return(PeerCommsState::ENABLED));
}

/**
 * Drains `from`'s emitted bytes into `to`'s RX queue and runs `to.exec()` to
 * feed the frame parser.
 */
inline void pumpCable(NativeSerialDriver& from, NativeSerialDriver& to) {
    const std::string bytes = from.getOutput();
    from.clearOutput();
    if (bytes.empty()) return;
    to.injectBytes(std::vector<uint8_t>(bytes.begin(), bytes.end()));
    to.exec();
}

/** Pushes a framed payload into `jack`'s RX queue and drains it via `exec()`. */
inline void deliverFrame(NativeSerialDriver& jack, const std::vector<uint8_t>& frame) {
    jack.injectBytes(frame);
    jack.exec();
}

/**
 * Serializes a PdnConnectionContext carrying `chainRole`, `userId`, `seqId`,
 * and `gameRole` into wire bytes for inbound context delivery.
 */
inline std::vector<uint8_t> pdnContextBytes(uint8_t chainRole, uint16_t userId, uint8_t seqId,
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

/**
 * Encodes a framed HELLO carrying `source` and, optionally, an advertised
 * `head` MAC and `flags` byte.
 */
inline std::vector<uint8_t> chainHelloFrame(const uint8_t* source, const uint8_t* head,
                                            uint8_t flags = 0) {
    HelloPayload hello{};
    memcpy(hello.source, source, 6);
    hello.deviceType = static_cast<uint8_t>(DeviceType::PDN);
    if (head != nullptr) memcpy(hello.headMac, head, 6);
    hello.flags = flags;
    return encodeFramed(hello);
}

/**
 * Shared single-device test harness for managers and tests that sit on top of a
 * real RemoteDeviceCoordinator and native jack drivers. Wires the fake platform
 * clock, radio mock defaults, jack wrappers, and inbound RDC packet-handler
 * captures so subclasses can drive topology through the production HELLO +
 * ESP-NOW receive paths.
 */
class RdcTestHarness : public testing::Test {
public:
    /**
     * Installs the fake clock, wires radio defaults and native jacks, captures
     * the RDC's inbound context and roster-channel handlers, and initializes
     * the coordinator in single-threaded mode.
     */
    void SetUp() override {
        fakeClock = new FakePlatformClock();
        SimpleTimer::setPlatformClock(fakeClock);
        fakeClock->setTime(1000);

        wireRadioDefaults(device, localMac);
        device.serialManager->setOutputJack(&outJack);
        device.serialManager->setInputJack(&inJack);
        device.serialManager->setSecondaryInputJack(&secondaryJack);

        ON_CALL(*device.mockPeerComms, setPacketHandler(testing::Eq(PktType::kPdnConnectionContext),
                                                        testing::_, testing::_))
        .WillByDefault(
        testing::DoAll(testing::SaveArg<1>(&contextHandler), testing::SaveArg<2>(&contextCtx)));
        ON_CALL(*device.mockPeerComms,
                setPacketHandler(testing::Eq(PktType::kConnectionAnnounce), testing::_, testing::_))
        .WillByDefault(
        testing::DoAll(testing::SaveArg<1>(&announceHandler), testing::SaveArg<2>(&announceCtx)));
        ON_CALL(*device.mockPeerComms,
                setPacketHandler(testing::Eq(PktType::kDisconnectReport), testing::_, testing::_))
        .WillByDefault(testing::DoAll(testing::SaveArg<1>(&disconnectReportHandler),
                                      testing::SaveArg<2>(&disconnectReportCtx)));

        rdc.setExternalConnectivityTask(true);
        rdc.initialize(device.wirelessManager, device.serialManager, &device);
    }

    /** Restores the platform clock after a test completes. */
    void TearDown() override {
        SimpleTimer::setPlatformClock(nullptr);
        delete fakeClock;
        fakeClock = nullptr;
    }

    /**
     * Feeds a peer's PdnConnectionContext in through the handler the reliable
     * transport registered with the radio driver — the production receive path.
     */
    void deliverPdnContext(const uint8_t* peerMac, bool isHunter = false) {
        if (contextHandler == nullptr) return;
        std::vector<uint8_t> bytes =
        pdnContextBytes(/*chainRole=*/0, /*userId=*/4242, ++contextSeqId, isHunter ? 1 : 0);
        contextHandler(peerMac, bytes.data(), bytes.size(), contextCtx);
    }

    /**
     * Feeds a member's ConnectionAnnouncePayload into the RDC's captured
     * receive handler when this device is acting as the chain/ring head.
     */
    void deliverConnectionAnnounce(const uint8_t* fromMac, const uint8_t* upstreamMac) {
        if (announceHandler == nullptr) return;
        ConnectionAnnouncePayload announce{};
        announce.seqId = ++announceSeqId;
        memcpy(announce.upstreamMac, upstreamMac, 6);
        announceHandler(fromMac, reinterpret_cast<const uint8_t*>(&announce), sizeof(announce),
                        announceCtx);
    }

    /**
     * Feeds a DisconnectReportPayload into the RDC's captured receive handler
     * to prune `disconnectedMac` and its downstream subtree from the head's
     * roster.
     */
    void deliverDisconnectReport(const uint8_t* fromMac, const uint8_t* disconnectedMac) {
        if (disconnectReportHandler == nullptr) return;
        DisconnectReportPayload report{};
        report.seqId = ++disconnectReportSeqId;
        memcpy(report.disconnectedMac, disconnectedMac, 6);
        disconnectReportHandler(fromMac, reinterpret_cast<const uint8_t*>(&report), sizeof(report),
                                disconnectReportCtx);
    }

    /**
     * Brings `jack` from Idle to Connected against `peerMac`, optionally
     * carrying an advertised chain head, peer role, and HELLO flag bits.
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
    NativeSerialDriver secondaryJack{"rdc-sec"};
    // Declared after the jacks so it is destroyed first; its dtor clears their
    // byte callbacks, mirroring production where the drivers outlive the RDC.
    RemoteDeviceCoordinator rdc;
    FakePlatformClock* fakeClock = nullptr;
    PeerCommsInterface::PacketCallback contextHandler = nullptr;
    void* contextCtx = nullptr;
    PeerCommsInterface::PacketCallback announceHandler = nullptr;
    void* announceCtx = nullptr;
    PeerCommsInterface::PacketCallback disconnectReportHandler = nullptr;
    void* disconnectReportCtx = nullptr;
    uint8_t contextSeqId = 0;
    uint8_t announceSeqId = 0;
    uint8_t disconnectReportSeqId = 0;
    uint8_t localMac[6] = {0x01, 0x00, 0x00, 0x00, 0x00, 0x00};
};
