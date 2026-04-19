#pragma once

// True multi-device chain duel test fixture.
//
// Each device owns its own MockDevice, RemoteDeviceCoordinator, Player, and
// ChainDuelManager. Packets emitted via mockPeerComms->sendData(...) are
// captured into a shared queue and routed by MAC to the target device's
// per-type packet handler. Physical serial connectivity between adjacent
// devices is simulated by driving the OutputIdleState's serial callback with
// SEND_MAC_ADDRESS and then injecting the EXCHANGE_ID handshake packets
// through each device's captured kHandshakeCommand handler (same pattern as
// the existing RDCTests::deliverPacketViaRDC helper, but routed per-device).
//
// Topology convention:
//   The task describes wiring as "device i's OUTPUT to device i+1's INPUT",
//   stating that hunter's opponent-jack is OUTPUT. Under the current
//   ChainDuelManager semantics, champion status requires NO same-role peer on
//   the opponent jack. For a H-H-H line, that means the champion is the
//   device whose OUTPUT jack is unconnected (the OUTPUT tail). To make
//   device 0 the natural "champion end", this fixture reverses the
//   per-index wiring: device 0 has nothing on its OUTPUT, device 1's OUTPUT
//   connects to device 0's INPUT, device 2's OUTPUT connects to device 1's
//   INPUT, etc. The supporter-jack chain at device 0 therefore contains
//   device 1 (direct) and device 2 (daisy), matching the A / S1 / S2
//   arrangement used by chainDuelThreeDeviceConfirm.

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <memory>
#include <vector>
#include <queue>
#include <cstring>

#include "device-mock.hpp"
#include "utility-tests.hpp"
#include "device/remote-device-coordinator.hpp"
#include "device/device-constants.hpp"
#include "game/chain-duel-manager.hpp"
#include "game/player.hpp"
#include "wireless/peer-comms-types.hpp"
#include "wireless/mac-functions.hpp"

using ::testing::_;
using ::testing::Return;
using ::testing::Invoke;
using ::testing::WithArgs;

// A single node in the multi-device harness.
struct MultiDeviceNode {
    std::unique_ptr<MockDevice> device;
    std::unique_ptr<RemoteDeviceCoordinator> rdc;
    std::unique_ptr<Player> player;
    std::unique_ptr<ChainDuelManager> cdm;

    // Per-device captured handlers (one slot per PktType the fixture routes).
    PeerCommsInterface::PacketCallback handshakeHandler = nullptr;
    void* handshakeCtx = nullptr;
    PeerCommsInterface::PacketCallback chainHandler = nullptr;
    void* chainCtx = nullptr;
    PeerCommsInterface::PacketCallback chainAckHandler = nullptr;
    void* chainAckCtx = nullptr;
    PeerCommsInterface::PacketCallback roleAnnounceHandler = nullptr;
    void* roleAnnounceCtx = nullptr;
    PeerCommsInterface::PacketCallback roleAnnounceAckHandler = nullptr;
    void* roleAnnounceAckCtx = nullptr;
    PeerCommsInterface::PacketCallback chainConfirmHandler = nullptr;
    void* chainConfirmCtx = nullptr;
    PeerCommsInterface::PacketCallback chainGameEventHandler = nullptr;
    void* chainGameEventCtx = nullptr;

    uint8_t mac[6] = {};
};

class ChainDuelMultiDeviceFixture : public ::testing::Test {
public:
    struct PendingPacket {
        size_t fromIndex;
        std::array<uint8_t, 6> toMac;
        PktType type;
        std::vector<uint8_t> data;
    };

    void SetUp() override {
        fakeClock = new FakePlatformClock();
        SimpleTimer::setPlatformClock(fakeClock);
        fakeClock->setTime(1000);
    }

    void TearDown() override {
        // Destroy nodes before the platform clock so any SimpleTimer dtors
        // that touch the clock still find it valid.
        nodes.clear();
        SimpleTimer::setPlatformClock(nullptr);
        delete fakeClock;
    }

    // Spawn N devices. Each gets a unique MAC. Physical connectivity is NOT
    // established here — call connectLinearHunterChain() or drive
    // connections manually via connectOutputOf(i).
    void spawnDevices(size_t count) {
        nodes.clear();
        nodes.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            auto node = std::make_unique<MultiDeviceNode>();
            node->mac[0] = 0x02; node->mac[1] = 0x00; node->mac[2] = 0x00;
            node->mac[3] = 0x00; node->mac[4] = 0x00; node->mac[5] = static_cast<uint8_t>(0x10 + i);

            node->device = std::make_unique<MockDevice>();
            node->rdc = std::make_unique<RemoteDeviceCoordinator>();
            node->player = std::make_unique<Player>();

            wirePeerCommsMock(*node);

            node->rdc->initialize(node->device->wirelessManager,
                                  node->device->serialManager,
                                  node->device.get());

            // CDM and chain-event handlers register after RDC so they don't
            // compete with RDC's own setPacketHandler calls.
            node->cdm = std::make_unique<ChainDuelManager>(node->player.get(),
                                                          node->device->wirelessManager,
                                                          node->rdc.get());
            wireChainEventHandlers(*node);

            // Hook CDM to RDC chain-change notifications (what Quickdraw does).
            ChainDuelManager* cdmRaw = node->cdm.get();
            node->rdc->setChainChangeCallback([cdmRaw]() {
                cdmRaw->onChainStateChanged();
            });

            nodes.push_back(std::move(node));
        }
    }

    // Convenience: set all players to hunters.
    void setAllHunters() {
        for (auto& n : nodes) n->player->setIsHunter(true);
    }

    // Wire every device's i+1 OUTPUT to device i INPUT (reverse of task
    // wording for the reason explained at the top of this header) so that
    // in a homogeneous hunter chain, device 0 is the champion.
    //
    // For i from 1 to N-1:
    //   device i OUTPUT <-> device (i-1) INPUT
    void connectLinearHunterChain() {
        for (size_t i = 1; i < nodes.size(); ++i) {
            connectOutputToPrev(i);
        }
    }

    // Drive the full serial+wireless handshake so device i's OUTPUT jack is
    // connected to device (i-1)'s INPUT jack. After this returns both nodes
    // have CONNECTED port status for the respective jack and the other's MAC
    // cached via HWM setMacPeer.
    void connectOutputToPrev(size_t i) {
        ASSERT_LT(i, nodes.size());
        ASSERT_GT(i, 0u);
        MultiDeviceNode& lower = *nodes[i - 1];   // receives on INPUT_JACK
        MultiDeviceNode& upper = *nodes[i];       // initiates on OUTPUT_JACK

        // Upper's OUTPUT_IDLE waits for serial SEND_MAC_ADDRESS from lower.
        // Lower's INPUT_IDLE would normally emit its MAC on input serial, which
        // physically reaches upper's output serial. Drive that directly.
        std::string macStr = MacToString(lower.mac);
        upper.device->outputJackSerial.stringCallback(
            SEND_MAC_ADDRESS + macStr + "#" +
            std::to_string(static_cast<int>(SerialIdentifier::INPUT_JACK)) +
            "t" + std::to_string(static_cast<int>(DeviceType::PDN)));
        upper.rdc->sync(upper.device.get());

        // Upper is now in OUTPUT_SEND_ID; it sent EXCHANGE_ID wirelessly to
        // lower during sync. Pump that packet into lower.
        deliverAllPackets();

        // Lower's InputIdleState registered upper and moved to INPUT_SEND_ID,
        // which itself sent EXCHANGE_ID back to upper. Pump.
        lower.rdc->sync(lower.device.get());
        upper.rdc->sync(upper.device.get());
        deliverAllPackets();

        // Final sync rounds — both sides commit CONNECTED.
        lower.rdc->sync(lower.device.get());
        upper.rdc->sync(upper.device.get());
        deliverAllPackets();
    }

    // Advance clock on all devices lockstep.
    void advanceClock(unsigned long ms) {
        fakeClock->advance(ms);
    }

    void syncAll() {
        for (auto& n : nodes) {
            n->rdc->sync(n->device.get());
            n->cdm->sync();
        }
    }

    // Pump all captured outgoing packets into the intended recipient's handlers
    // until the queue is empty. Safe to call iteratively — any new packets
    // produced by receivers get queued and pumped in subsequent passes.
    void deliverAllPackets(int maxRounds = 32) {
        int rounds = 0;
        while (!pending.empty() && rounds++ < maxRounds) {
            std::queue<PendingPacket> batch;
            std::swap(batch, pending);
            while (!batch.empty()) {
                PendingPacket p = std::move(batch.front());
                batch.pop();
                dispatch(p);
            }
        }
        // One final drain attempt — if new packets appeared, pump once more.
        while (!pending.empty() && rounds++ < maxRounds) {
            PendingPacket p = std::move(pending.front());
            pending.pop();
            dispatch(p);
        }
    }

    MultiDeviceNode& node(size_t i) { return *nodes[i]; }
    size_t nodeCount() const { return nodes.size(); }

protected:
    std::vector<std::unique_ptr<MultiDeviceNode>> nodes;
    std::queue<PendingPacket> pending;
    FakePlatformClock* fakeClock = nullptr;

    size_t indexOfMac(const uint8_t* mac) const {
        for (size_t i = 0; i < nodes.size(); ++i) {
            if (memcmp(nodes[i]->mac, mac, 6) == 0) return i;
        }
        return SIZE_MAX;
    }

    void dispatch(const PendingPacket& p) {
        size_t toIdx = indexOfMac(p.toMac.data());
        if (toIdx == SIZE_MAX) return;  // MAC unknown — drop (mirrors real ESP-NOW gating).

        MultiDeviceNode& target = *nodes[toIdx];
        MultiDeviceNode& source = *nodes[p.fromIndex];

        PeerCommsInterface::PacketCallback handler = nullptr;
        void* ctx = nullptr;
        switch (p.type) {
            case PktType::kHandshakeCommand:     handler = target.handshakeHandler;        ctx = target.handshakeCtx; break;
            case PktType::kChainAnnouncement:    handler = target.chainHandler;            ctx = target.chainCtx; break;
            case PktType::kChainAnnouncementAck: handler = target.chainAckHandler;         ctx = target.chainAckCtx; break;
            case PktType::kRoleAnnounce:         handler = target.roleAnnounceHandler;     ctx = target.roleAnnounceCtx; break;
            case PktType::kRoleAnnounceAck:      handler = target.roleAnnounceAckHandler;  ctx = target.roleAnnounceAckCtx; break;
            case PktType::kChainConfirm:         handler = target.chainConfirmHandler;     ctx = target.chainConfirmCtx; break;
            case PktType::kChainGameEvent:       handler = target.chainGameEventHandler;   ctx = target.chainGameEventCtx; break;
            default: return;
        }
        if (!handler) return;
        handler(source.mac, p.data.data(), p.data.size(), ctx);
    }

    void wirePeerCommsMock(MultiDeviceNode& n) {
        auto* pc = n.device->mockPeerComms;

        ON_CALL(*pc, getMacAddress()).WillByDefault(Return(n.mac));
        ON_CALL(*pc, addEspNowPeer(_)).WillByDefault(Return(0));
        ON_CALL(*pc, removeEspNowPeer(_)).WillByDefault(Return(0));
        ON_CALL(*pc, getPeerCommsState()).WillByDefault(Return(PeerCommsState::CONNECTED));

        // Capture every outgoing packet into the shared queue.
        size_t selfIdx = nodes.size();  // this node's index once push_back completes
        ON_CALL(*pc, sendData(_, _, _, _))
            .WillByDefault([this, selfIdx](const uint8_t* dst, PktType type,
                                           const uint8_t* data, const size_t len) -> int {
                PendingPacket p;
                p.fromIndex = selfIdx;
                memcpy(p.toMac.data(), dst, 6);
                p.type = type;
                p.data.assign(data, data + len);
                pending.push(std::move(p));
                return 1;
            });

        // Capture packet handlers as they are registered (RDC, then CDM-side
        // shims below). Each PktType saves into its own slot.
        ON_CALL(*pc, setPacketHandler(testing::Eq(PktType::kHandshakeCommand), _, _))
            .WillByDefault([&n](PktType, PeerCommsInterface::PacketCallback cb, void* ctx) {
                n.handshakeHandler = cb; n.handshakeCtx = ctx;
            });
        ON_CALL(*pc, setPacketHandler(testing::Eq(PktType::kChainAnnouncement), _, _))
            .WillByDefault([&n](PktType, PeerCommsInterface::PacketCallback cb, void* ctx) {
                n.chainHandler = cb; n.chainCtx = ctx;
            });
        ON_CALL(*pc, setPacketHandler(testing::Eq(PktType::kChainAnnouncementAck), _, _))
            .WillByDefault([&n](PktType, PeerCommsInterface::PacketCallback cb, void* ctx) {
                n.chainAckHandler = cb; n.chainAckCtx = ctx;
            });
        ON_CALL(*pc, setPacketHandler(testing::Eq(PktType::kRoleAnnounce), _, _))
            .WillByDefault([&n](PktType, PeerCommsInterface::PacketCallback cb, void* ctx) {
                n.roleAnnounceHandler = cb; n.roleAnnounceCtx = ctx;
            });
        ON_CALL(*pc, setPacketHandler(testing::Eq(PktType::kRoleAnnounceAck), _, _))
            .WillByDefault([&n](PktType, PeerCommsInterface::PacketCallback cb, void* ctx) {
                n.roleAnnounceAckHandler = cb; n.roleAnnounceAckCtx = ctx;
            });
        ON_CALL(*pc, setPacketHandler(testing::Eq(PktType::kChainConfirm), _, _))
            .WillByDefault([&n](PktType, PeerCommsInterface::PacketCallback cb, void* ctx) {
                n.chainConfirmHandler = cb; n.chainConfirmCtx = ctx;
            });
        ON_CALL(*pc, setPacketHandler(testing::Eq(PktType::kChainGameEvent), _, _))
            .WillByDefault([&n](PktType, PeerCommsInterface::PacketCallback cb, void* ctx) {
                n.chainGameEventHandler = cb; n.chainGameEventCtx = ctx;
            });
    }

    // Mirrors what Quickdraw::Quickdraw installs for the chain-duel packet
    // types. CDM doesn't install these itself, so for a driver-less fixture
    // we register trampolines that deliver received packets into the CDM.
    void wireChainEventHandlers(MultiDeviceNode& n) {
        ChainDuelManager* cdm = n.cdm.get();

        n.device->wirelessManager->setEspNowPacketHandler(
            PktType::kRoleAnnounce,
            [](const uint8_t* fromMac, const uint8_t* data, const size_t dataLen, void* ctx) {
                if (dataLen != sizeof(RoleAnnouncePayload)) return;
                const RoleAnnouncePayload* p = reinterpret_cast<const RoleAnnouncePayload*>(data);
                static_cast<ChainDuelManager*>(ctx)->onRoleAnnounceReceived(
                    fromMac, p->role, p->championMac, p->seqId);
            },
            cdm);

        n.device->wirelessManager->setEspNowPacketHandler(
            PktType::kRoleAnnounceAck,
            [](const uint8_t* fromMac, const uint8_t* data, const size_t dataLen, void* ctx) {
                if (dataLen != sizeof(RoleAnnounceAckPayload)) return;
                const RoleAnnounceAckPayload* p = reinterpret_cast<const RoleAnnounceAckPayload*>(data);
                static_cast<ChainDuelManager*>(ctx)->onRoleAnnounceAckReceived(fromMac, p->seqId);
            },
            cdm);

        n.device->wirelessManager->setEspNowPacketHandler(
            PktType::kChainConfirm,
            [](const uint8_t* fromMac, const uint8_t* data, const size_t dataLen, void* ctx) {
                if (dataLen != sizeof(ChainConfirmPayload)) return;
                const ChainConfirmPayload* p = reinterpret_cast<const ChainConfirmPayload*>(data);
                static_cast<ChainDuelManager*>(ctx)->onConfirmReceived(
                    fromMac, p->originatorMac, p->seqId);
            },
            cdm);

        n.device->wirelessManager->setEspNowPacketHandler(
            PktType::kChainGameEvent,
            [](const uint8_t*, const uint8_t*, const size_t, void*) {
                // No-op at fixture level — CDM doesn't consume this directly;
                // Quickdraw routes it to SupporterReady. Tests that need it can
                // register their own handler via the node's peerComms mock.
            },
            cdm);
    }
};

// ============================================================================
// Tests exercising the multi-device fixture.
// ============================================================================

// H-H-H linear chain: device 0 is champion (OUTPUT tail), devices 1 and 2 are
// supporters whose championMac caches device 0's MAC after the role-announce
// cascade propagates.
inline void cdmMultiDeviceChainFormsAndElectsChampion(ChainDuelMultiDeviceFixture* suite) {
    suite->spawnDevices(3);
    suite->setAllHunters();
    suite->connectLinearHunterChain();

    // Pump any lingering RDC chain announcements.
    suite->deliverAllPackets();
    suite->syncAll();
    suite->deliverAllPackets();

    MultiDeviceNode& d0 = suite->node(0);
    MultiDeviceNode& d1 = suite->node(1);
    MultiDeviceNode& d2 = suite->node(2);

    // Role election: device 0 has no OUTPUT-jack peer so isChampion() should be
    // true once it observes its supporter-jack peer. Drive onChainStateChanged
    // explicitly to match what Quickdraw does.
    d0.cdm->onChainStateChanged();
    d1.cdm->onChainStateChanged();
    d2.cdm->onChainStateChanged();
    suite->deliverAllPackets();
    // A second pass lets the cascaded role-announce from d1 reach d2.
    d0.cdm->onChainStateChanged();
    d1.cdm->onChainStateChanged();
    d2.cdm->onChainStateChanged();
    suite->deliverAllPackets();

    EXPECT_TRUE(d0.cdm->isChampion());
    EXPECT_FALSE(d1.cdm->isChampion());
    EXPECT_FALSE(d2.cdm->isChampion());

    EXPECT_TRUE(d1.cdm->isSupporter());
    EXPECT_TRUE(d2.cdm->isSupporter());

    // Both supporters should have learned d0's MAC as championMac_.
    ASSERT_NE(d1.cdm->getChampionMac(), nullptr);
    EXPECT_EQ(memcmp(d1.cdm->getChampionMac(), d0.mac, 6), 0);
    ASSERT_NE(d2.cdm->getChampionMac(), nullptr);
    EXPECT_EQ(memcmp(d2.cdm->getChampionMac(), d0.mac, 6), 0);
}

// H-H-H linear chain: after cascade, device 2 sendConfirm produces a
// kChainConfirm addressed to device 0, which when routed through
// deliverAllPackets hits d0's CDM and increments the boost.
inline void cdmMultiDeviceConfirmDeliveredToChampion(ChainDuelMultiDeviceFixture* suite) {
    suite->spawnDevices(3);
    suite->setAllHunters();
    suite->connectLinearHunterChain();
    suite->deliverAllPackets();
    suite->syncAll();
    suite->deliverAllPackets();

    MultiDeviceNode& d0 = suite->node(0);
    MultiDeviceNode& d1 = suite->node(1);
    MultiDeviceNode& d2 = suite->node(2);

    d0.cdm->onChainStateChanged();
    d1.cdm->onChainStateChanged();
    d2.cdm->onChainStateChanged();
    suite->deliverAllPackets();
    d0.cdm->onChainStateChanged();
    d1.cdm->onChainStateChanged();
    d2.cdm->onChainStateChanged();
    suite->deliverAllPackets();

    ASSERT_TRUE(d0.cdm->isChampion());
    ASSERT_NE(d2.cdm->getChampionMac(), nullptr);
    ASSERT_EQ(memcmp(d2.cdm->getChampionMac(), d0.mac, 6), 0);
    ASSERT_EQ(d0.cdm->getBoostMs(), 0u);

    // D2 sends confirm — payload targets d0's MAC directly; fixture router
    // delivers it to d0's kChainConfirm handler → CDM::onConfirmReceived.
    d2.cdm->sendConfirm();
    suite->deliverAllPackets();

    EXPECT_EQ(d0.cdm->getConfirmedSupporterCount(), 1u);
    EXPECT_EQ(d0.cdm->getBoostMs(), ChainDuelManager::BOOST_PER_SUPPORTER_MS);
}
