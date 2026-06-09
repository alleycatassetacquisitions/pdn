#pragma once

#include <algorithm>

// True multi-device chain duel test fixture.
//
// Each device owns its own MockDevice, RemoteDeviceCoordinator, Player, and
// ChainManager. Packets emitted via mockPeerComms->sendData(...) are
// captured into a shared queue and routed by MAC to the target device's
// per-type packet handler. Physical serial connectivity between adjacent
// devices is simulated by feeding binary HELLO frames into each receiver's
// byte parser (deliverHello), then routing subsequent BEACON floods over the
// recorded serial links via propagateSerialMessages().
//
// Topology convention:
//   A hunter's opponent-jack is OUTPUT, so the natural wiring is "device i's
//   OUTPUT to device i+1's INPUT". Under the current
//   ChainManager semantics, champion status requires NO same-role peer on
//   the opponent jack. For a H-H-H line, that means the champion is the
//   device whose OUTPUT jack is unconnected (the OUTPUT tail). To make
//   device 0 the natural "champion end", this fixture reverses the
//   per-index wiring: device 0 has nothing on its OUTPUT, device 1's OUTPUT
//   connects to device 0's INPUT, device 2's OUTPUT connects to device 1's
//   INPUT, etc. The supporter-jack chain at device 0 therefore contains
//   device 1 (direct) and device 2 (two hops out), matching the A / S1 / S2
//   arrangement used by chainMultiDeviceConfirmDeliveredToChampion.

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <memory>
#include <vector>
#include <queue>
#include <cstring>

#include "device-mock.hpp"
#include "utility-tests.hpp"
#include "device/remote-device-coordinator.hpp"
#include "device/peer-graph-codec.hpp"
#include "game/chain-manager.hpp"
#include "game/shootout-manager.hpp"
#include "game/player.hpp"
#include "device/drivers/peer-comms-types.hpp"
#include "wireless/resender.hpp"
#include "wireless/wireless-transport.hpp"

using ::testing::_;
using ::testing::Return;
using ::testing::Invoke;
using ::testing::WithArgs;

struct MultiDeviceNode {
    std::unique_ptr<MockDevice> device;
    std::unique_ptr<RemoteDeviceCoordinator> rdc;
    std::unique_ptr<Player> player;
    std::unique_ptr<WirelessTransport> transport;
    std::unique_ptr<ChainManager> chainManager;
    std::unique_ptr<ShootoutManager> shootout;

    // Per-device captured handlers (one slot per PktType the fixture routes).
    PeerCommsInterface::PacketCallback chainConfirmHandler = nullptr;
    void* chainConfirmCtx = nullptr;
    PeerCommsInterface::PacketCallback chainGameEventHandler = nullptr;
    void* chainGameEventCtx = nullptr;
    PeerCommsInterface::PacketCallback shootoutHandler = nullptr;
    void* shootoutCtx = nullptr;
    PeerCommsInterface::PacketCallback ackHandler = nullptr;  // unified kAck
    void* ackCtx = nullptr;

    uint8_t mac[6] = {};
};

class ChainMultiDeviceFixture : public ::testing::Test {
public:
    struct PendingPacket {
        size_t fromIndex;
        std::array<uint8_t, 6> toMac;
        PktType type;
        std::vector<uint8_t> data;
    };

    // A physical serial wire: bytes written by the sender's jack are
    // delivered to the receiver's jack string callback. One link per
    // direction (real RS-232-style full-duplex over separate conductors).
    struct SerialLink {
        size_t senderNode;
        SerialIdentifier senderJack;  // jack that writes
        size_t receiverNode;
        SerialIdentifier receiverJack;  // jack whose callback fires
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

    // Physical connectivity is NOT established here; call
    // connectLinearHunterChain() or drive connections manually via
    // connectOutputToPrev(i).
    void spawnDevices(size_t count) {
        nodes.clear();
        serialLinks_.clear();
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
            // The fixture advances the fake clock in ~1s steps without driving
            // HELLOs every tick, so the 30ms production silent-link would
            // false-fire and churn the graph. Push it out; topology tests
            // exercise cable connectivity, not silent-link timing.
            node->rdc->setJackDeadSilentLinkMsForTest(60000);

            // Point MockDevice's RDC getter at the real coordinator so RDC
            // HELLO ingest reaches the right object via
            // getRemoteDeviceCoordinator(). Without this, RDC mutations
            // would target FakeRemoteDeviceCoordinator
            // whose serialManager is null, silently swallowing emits.
            node->device->setRdcOverride(node->rdc.get());

            // Flood each device's hunter/bounty role in its BEACON so the peer-graph can
            // derive champion/supporter (production wires this in quickdraw.cpp).
            // Reads the player live, so roles set after spawn still propagate.
            Player* playerRaw = node->player.get();
            node->rdc->setRoleProvider(
                [playerRaw] { return chain_role::byteFor(playerRaw->isHunter()); });

            // ChainManager and chain-event handlers register after RDC so they don't
            // compete with RDC's own setPacketHandler calls.
            node->transport = std::make_unique<WirelessTransport>(
                node->device->wirelessManager);
            node->chainManager = std::make_unique<ChainManager>(node->player.get(),
                                                          node->device->wirelessManager,
                                                          node->rdc.get());
            node->chainManager->initialize(node->transport.get());
            node->shootout = std::make_unique<ShootoutManager>(node->player.get(),
                                                              node->device->wirelessManager,
                                                              node->rdc.get(),
                                                              node->chainManager.get());
            node->shootout->initialize(node->transport.get());

            // Hook ChainManager to RDC direct-peer transitions (what Quickdraw does).
            // ShootoutManager intentionally not wired; advanceClock() crosses
            // the HELLO silent-link threshold and would fire it spuriously.
            ChainManager* chainRaw = node->chainManager.get();
            node->rdc->setOnDirectPeerChange(
                [chainRaw](SerialIdentifier port,
                         std::optional<RemoteDeviceCoordinator::Peer> prev,
                         std::optional<RemoteDeviceCoordinator::Peer> curr) {
                    chainRaw->onDirectPeerChange(port, prev, curr);
                });

            nodes.push_back(std::move(node));
        }
    }

    void setAllHunters() {
        for (auto& n : nodes) n->player->setIsHunter(true);
    }

    // Wire each device's OUTPUT to the previous device's INPUT (reversed
    // wiring; see the header comment) so that in a homogeneous hunter chain,
    // device 0 is the champion.
    void connectLinearHunterChain() {
        for (size_t i = 1; i < nodes.size(); ++i) {
            connectOutputToPrev(i);
        }
    }

    // Deliver a HELLO frame onto the given node's jack as if the partner had
    // just transmitted it over the serial cable. RDC parses it and sets
    // macPeer for the local jack. Mirrors production exactly.
    void deliverHello(MockDevice& target, SerialIdentifier localJack,
                      const uint8_t* peerMac,
                      uint8_t deviceType = static_cast<uint8_t>(DeviceType::PDN),
                      uint16_t userId = peer_graph::kUserIdUnknown) {
        net::Mac mac;
        std::copy_n(peerMac, 6, mac.begin());
        auto frame = peer_graph::encodeHello(mac, deviceType, userId);
        auto& serial = localJack == SerialIdentifier::INPUT_JACK
            ? target.inputJackSerial : target.outputJackSerial;
        ASSERT_NE(serial.bytesCallback, nullptr);
        serial.bytesCallback(frame.data(), frame.size());
        // ingestSerial only enqueues; apply here so the delivered frame takes
        // effect (macPeer set) without requiring a full sync().
        target.getRemoteDeviceCoordinator()->exec();
    }

    // Deliver serial HELLOs both ways so device i's OUTPUT jack is
    // connected to device (i-1)'s INPUT jack. After this returns both nodes
    // have CONNECTED port status for the respective jack and the other's MAC
    // cached via DirectPeerTable::setMacPeer.
    void connectOutputToPrev(size_t i) {
        ASSERT_LT(i, nodes.size());
        ASSERT_GT(i, 0u);
        MultiDeviceNode& lower = *nodes[i - 1];   // receives on INPUT_JACK
        MultiDeviceNode& upper = *nodes[i];       // initiates on OUTPUT_JACK

        // Record full-duplex serial links so propagateSerialMessages() can
        // route peer-graph HELLO/BEACON frames between the now-connected jacks.
        serialLinks_.push_back({i,     SerialIdentifier::OUTPUT_JACK,
                                 i - 1, SerialIdentifier::INPUT_JACK});
        serialLinks_.push_back({i - 1, SerialIdentifier::INPUT_JACK,
                                 i,     SerialIdentifier::OUTPUT_JACK});

        // Deliver HELLO frames in both directions, mirroring production:
        // each side's serviceConnectivity emits HELLO, the partner's RDC
        // parses it and sets macPeer for that jack.
        deliverHello(*upper.device, SerialIdentifier::OUTPUT_JACK,
                      lower.mac);
        deliverHello(*lower.device, SerialIdentifier::INPUT_JACK,
                      upper.mac);

        // Sync drains the macPeer null→set edge: ESP-NOW peer registration,
        // onDirectPeerChange fire.
        lower.rdc->sync();
        upper.rdc->sync();
        propagateSerialMessages();
    }

    // Wire two devices' "tail" jacks together (AUX-to-AUX cable in a mixed-loop
    // assembly). Hunter tail = node with INPUT free; bounty tail = node with
    // OUTPUT free. Does NOT trigger a duel (different-role jacks face each other).
    //
    // Mirrors connectOutputToPrev exactly: HELLO delivered on the bounty
    // tail's OUTPUT jack and the hunter tail's INPUT jack, then wireless
    // packets pumped through deliverAllPackets as normal.
    void connectTailToTail(size_t hunterTailIdx, size_t bountyTailIdx) {
        ASSERT_LT(hunterTailIdx, nodes.size());
        ASSERT_LT(bountyTailIdx, nodes.size());
        MultiDeviceNode& hunterTail = *nodes[hunterTailIdx];  // INPUT free (lower)
        MultiDeviceNode& bountyTail = *nodes[bountyTailIdx];  // OUTPUT free (upper)

        // Full-duplex serial links: bounty OUTPUT → hunter INPUT and back.
        serialLinks_.push_back({bountyTailIdx, SerialIdentifier::OUTPUT_JACK,
                                 hunterTailIdx, SerialIdentifier::INPUT_JACK});
        serialLinks_.push_back({hunterTailIdx, SerialIdentifier::INPUT_JACK,
                                 bountyTailIdx, SerialIdentifier::OUTPUT_JACK});

        deliverHello(*bountyTail.device, SerialIdentifier::OUTPUT_JACK,
                      hunterTail.mac);
        deliverHello(*hunterTail.device, SerialIdentifier::INPUT_JACK,
                      bountyTail.mac);

        hunterTail.rdc->sync();
        bountyTail.rdc->sync();
        propagateSerialMessages();
    }

    // Advance clock on all devices lockstep.
    void advanceClock(unsigned long ms) {
        fakeClock->advance(ms);
    }

    // Settle every node's topology so consumers gated on isTopologyStable()
    // (the confirm gate, buildLoopMemberSet) accept work. Eight 1.1s ticks
    // bracket the 200ms peer-graph stability clock and ChainManager's
    // 1-cycle coordinator guard with margin.
    void primeTopologyStableAll() {
        for (int i = 0; i < 8; ++i) {
            advanceClock(1100);
            syncAll();
        }
    }

    void syncAll() {
        for (auto& n : nodes) {
            n->rdc->sync();
            n->chainManager->sync();
            n->shootout->sync();
            // The platform loop owns the resender pump (main.cpp/cli-main.cpp);
            // mirror it here so reliable retries fire under clock advance.
            n->transport->sync();
        }
        propagateSerialMessages();
    }

    // Route serial bytes across connected jacks into the receiver's byte-level
    // bytesCallback, which drives RDC's frame parser for binary HELLO/BEACON
    // frames.
    //
    // Loops until no link produces a new message in a pass, so event-driven
    // re-emits (the BEACON flood writing to the opposite jack) propagate
    // fully within a single call.
    void propagateSerialMessages() {
        bool anyDelivered = true;
        int guard = 64;
        while (anyDelivered && guard-- > 0) {
            anyDelivered = false;
            // Apply enqueued frames first: HELLO -> macPeer, BEACON -> accept +
            // flood (writes flood bytes to the opposite jack, routed next pass).
            // ingestSerial only enqueues, so floods cascade through here.
            for (auto& n : nodes) n->rdc->exec();
            for (auto& link : serialLinks_) {
                MultiDeviceNode& sender   = *nodes[link.senderNode];
                MultiDeviceNode& receiver = *nodes[link.receiverNode];
                FakeHWSerialWrapper& senderHW = (link.senderJack == SerialIdentifier::OUTPUT_JACK)
                    ? sender.device->outputJackSerial
                    : sender.device->inputJackSerial;
                FakeHWSerialWrapper& receiverHW = (link.receiverJack == SerialIdentifier::OUTPUT_JACK)
                    ? receiver.device->outputJackSerial
                    : receiver.device->inputJackSerial;
                if (!senderHW.available()) continue;

                std::vector<uint8_t> bytes;
                bytes.reserve(senderHW.msgQueue.size());
                while (!senderHW.msgQueue.empty()) {
                    bytes.push_back(static_cast<uint8_t>(senderHW.msgQueue.front()));
                    senderHW.msgQueue.pop_front();
                }
                if (bytes.empty()) continue;
                anyDelivered = true;

                if (receiverHW.bytesCallback) {
                    receiverHW.bytesCallback(bytes.data(), bytes.size());
                }
            }
        }
    }

    // Drive ChainConfirm from every supporter up to the champion. The champion
    // needs confirmedSupporters_ populated before the ring closes or loop
    // closure won't be detected, so without this the coordinator-claim path
    // never fires in fixture tests.
    void pumpSupporterConfirms() {
        // Caller must converge BEACON-derived roles first (onChainStateChanged
        // + deliverAllPackets) so each supporter's championMac_ is set.
        for (auto& n : nodes) {
            n->chainManager->sendConfirm();
        }
        deliverAllPackets();
    }

    // Close a linear chain into a ring by wiring the tail's INPUT into the
    // head's OUTPUT. Mirroring on both endpoints matches real hardware, where
    // plugging a cable produces a serial arrival at both ends; single-ended
    // injection would leave the tail's jack without its HELLO.
    void closeRing() {
        ASSERT_GE(nodes.size(), 2u);
        size_t tail = nodes.size() - 1;
        MultiDeviceNode& head = *nodes[0];
        MultiDeviceNode& tailNode = *nodes[tail];

        // Record the ring-close serial links so peer-graph HELLO/BEACON frames
        // propagate across the closing cable after it goes CONNECTED.
        serialLinks_.push_back({0,    SerialIdentifier::OUTPUT_JACK,
                                 tail, SerialIdentifier::INPUT_JACK});
        serialLinks_.push_back({tail, SerialIdentifier::INPUT_JACK,
                                 0,   SerialIdentifier::OUTPUT_JACK});

        deliverHello(*head.device, SerialIdentifier::OUTPUT_JACK,
                      tailNode.mac);
        deliverHello(*tailNode.device, SerialIdentifier::INPUT_JACK,
                      head.mac);

        head.rdc->sync();
        tailNode.rdc->sync();
        propagateSerialMessages();

        syncAll();
        propagateSerialMessages();
    }

    // Simulate a physical cable yank between two adjacent ring members.
    // Removes the full-duplex serial links so no further bytes (HELLOs) cross,
    // then declares the specific yanked jack dead on each side via the same
    // teardown path the production silent-link watchdog drives. Surgical to the
    // named jack (the fixture pins the silent-link threshold to 60s, so a
    // clock-based expiry would risk killing the node's other, still-connected
    // jack). declareJackDead drops the now-dead edge from the peer-graph; the
    // next sync re-emits BEACONs that propagate the break around the ring.
    void breakSerialLink(size_t nodeA, SerialIdentifier jackA,
                         size_t nodeB, SerialIdentifier jackB) {
        serialLinks_.erase(
            std::remove_if(serialLinks_.begin(), serialLinks_.end(),
                [&](const SerialLink& l) {
                    return (l.senderNode == nodeA && l.senderJack == jackA &&
                            l.receiverNode == nodeB && l.receiverJack == jackB) ||
                           (l.senderNode == nodeB && l.senderJack == jackB &&
                            l.receiverNode == nodeA && l.receiverJack == jackA);
                }),
            serialLinks_.end());
        nodes[nodeA]->rdc->declareJackDeadForTest(jackA);
        nodes[nodeB]->rdc->declareJackDeadForTest(jackB);
    }

    // Pump captured outgoing packets into recipients' handlers until the queue
    // is empty. Packets produced by receivers get queued and pumped next pass.
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
        // One final drain attempt: if new packets appeared, pump once more.
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
    std::vector<SerialLink> serialLinks_;
    FakePlatformClock* fakeClock = nullptr;

    size_t indexOfMac(const uint8_t* mac) const {
        for (size_t i = 0; i < nodes.size(); ++i) {
            if (memcmp(nodes[i]->mac, mac, 6) == 0) return i;
        }
        return SIZE_MAX;
    }

    void dispatch(const PendingPacket& p) {
        size_t toIdx = indexOfMac(p.toMac.data());
        if (toIdx == SIZE_MAX) return;  // unknown MAC: drop, mirroring ESP-NOW gating

        MultiDeviceNode& target = *nodes[toIdx];
        MultiDeviceNode& source = *nodes[p.fromIndex];

        PeerCommsInterface::PacketCallback handler = nullptr;
        void* ctx = nullptr;
        switch (p.type) {
            case PktType::kChainConfirm:           handler = target.chainConfirmHandler;           ctx = target.chainConfirmCtx; break;
            case PktType::kChainGameEvent:         handler = target.chainGameEventHandler;         ctx = target.chainGameEventCtx; break;
            case PktType::kShootoutCommand:        handler = target.shootoutHandler;               ctx = target.shootoutCtx; break;
            case PktType::kAck:                    handler = target.ackHandler;                    ctx = target.ackCtx; break;
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

        // Capture packet handlers as they are registered (RDC, then ChainManager-side
        // shims below). Each PktType saves into its own slot.
        ON_CALL(*pc, setPacketHandler(testing::Eq(PktType::kChainConfirm), _, _))
            .WillByDefault([&n](PktType, PeerCommsInterface::PacketCallback cb, void* ctx) {
                n.chainConfirmHandler = cb; n.chainConfirmCtx = ctx;
            });
        ON_CALL(*pc, setPacketHandler(testing::Eq(PktType::kChainGameEvent), _, _))
            .WillByDefault([&n](PktType, PeerCommsInterface::PacketCallback cb, void* ctx) {
                n.chainGameEventHandler = cb; n.chainGameEventCtx = ctx;
            });
        ON_CALL(*pc, setPacketHandler(testing::Eq(PktType::kShootoutCommand), _, _))
            .WillByDefault([&n](PktType, PeerCommsInterface::PacketCallback cb, void* ctx) {
                n.shootoutHandler = cb; n.shootoutCtx = ctx;
            });
        ON_CALL(*pc, setPacketHandler(testing::Eq(PktType::kAck), _, _))
            .WillByDefault([&n](PktType, PeerCommsInterface::PacketCallback cb, void* ctx) {
                n.ackHandler = cb; n.ackCtx = ctx;
            });
    }

    // WirelessTransport wires kAck at construction and each data PktType when
    // its first channel is claimed, so the fixture installs no handlers of its
    // own; wirePeerCommsMock only captures what the transport registers.

};

// H-H-H linear chain: device 0 is champion (OUTPUT tail), devices 1 and 2 are
// supporters whose championMac caches device 0's MAC once the BEACON flood
// propagates roles.
inline void chainMultiDeviceChainFormsAndElectsChampion(ChainMultiDeviceFixture* suite) {
    suite->spawnDevices(3);
    suite->setAllHunters();
    suite->connectLinearHunterChain();

    suite->deliverAllPackets();
    suite->syncAll();
    suite->deliverAllPackets();

    MultiDeviceNode& d0 = suite->node(0);
    MultiDeviceNode& d1 = suite->node(1);
    MultiDeviceNode& d2 = suite->node(2);

    // device 0 has no OUTPUT-jack peer so it becomes champion once it sees its
    // supporter-jack peer. Drive onChainStateChanged as Quickdraw does.
    d0.chainManager->onChainStateChanged();
    d1.chainManager->onChainStateChanged();
    d2.chainManager->onChainStateChanged();
    suite->deliverAllPackets();
    // Second pass lets d1's updated BEACON role reach d2.
    d0.chainManager->onChainStateChanged();
    d1.chainManager->onChainStateChanged();
    d2.chainManager->onChainStateChanged();
    suite->deliverAllPackets();

    EXPECT_TRUE(d0.chainManager->isChampion());
    EXPECT_FALSE(d1.chainManager->isChampion());
    EXPECT_FALSE(d2.chainManager->isChampion());

    EXPECT_TRUE(d1.chainManager->isSupporter());
    EXPECT_TRUE(d2.chainManager->isSupporter());

    auto champ1 = d1.chainManager->getChampionMac();
    ASSERT_TRUE(champ1.has_value());
    EXPECT_EQ(memcmp(champ1->data(), d0.mac, 6), 0);
    auto champ2 = d2.chainManager->getChampionMac();
    ASSERT_TRUE(champ2.has_value());
    EXPECT_EQ(memcmp(champ2->data(), d0.mac, 6), 0);
}

// H-H-H linear chain: after cascade, device 2 sendConfirm produces a
// kChainConfirm addressed to device 0, which when routed through
// deliverAllPackets hits d0's ChainManager and increments the boost.
inline void chainMultiDeviceConfirmDeliveredToChampion(ChainMultiDeviceFixture* suite) {
    suite->spawnDevices(3);
    suite->setAllHunters();
    suite->connectLinearHunterChain();
    suite->deliverAllPackets();
    suite->syncAll();
    suite->deliverAllPackets();

    MultiDeviceNode& d0 = suite->node(0);
    MultiDeviceNode& d1 = suite->node(1);
    MultiDeviceNode& d2 = suite->node(2);

    // Converge BEACON-derived roles. Auto-fired ChainConfirms during this
    // window may be dropped by the stability gate at d0 (the receiver's
    // topology snapshot is still settling). The test's contract is the dedup
    // behavior, verified via explicit re-confirms after stability primes.
    d0.chainManager->onChainStateChanged();
    d1.chainManager->onChainStateChanged();
    d2.chainManager->onChainStateChanged();
    suite->deliverAllPackets();
    d0.chainManager->onChainStateChanged();
    d1.chainManager->onChainStateChanged();
    d2.chainManager->onChainStateChanged();
    suite->deliverAllPackets();

    ASSERT_TRUE(d0.chainManager->isChampion());
    auto champ2 = d2.chainManager->getChampionMac();
    ASSERT_TRUE(champ2.has_value());
    ASSERT_EQ(memcmp(champ2->data(), d0.mac, 6), 0);

    // Prime topology stability then explicitly fire confirms from each
    // supporter. After cascade is settled both d1 and d2 have championMac=d0;
    // sendConfirm targets d0 directly. Skip d0: the champion's championMac
    // is self, so its sendConfirm would loop back and inflate the count.
    suite->primeTopologyStableAll();
    d1.chainManager->sendConfirm();
    d2.chainManager->sendConfirm();
    suite->deliverAllPackets();

    EXPECT_EQ(d0.chainManager->getConfirmedSupporterCount(), 2u);
    EXPECT_EQ(d0.chainManager->getBoostMs(), 2 * ChainManager::BOOST_PER_SUPPORTER_MS);

    // A redundant explicit confirm from d2 must dedup by MAC; count/boost
    // stay unchanged.
    d2.chainManager->sendConfirm();
    suite->deliverAllPackets();
    EXPECT_EQ(d0.chainManager->getConfirmedSupporterCount(), 2u);
    EXPECT_EQ(d0.chainManager->getBoostMs(), 2 * ChainManager::BOOST_PER_SUPPORTER_MS);
}

// Production auto-confirm path: a supporter fires ChainConfirm automatically
// when its championMac_ resolves from BEACON-derived roles; no explicit
// user press / sendConfirm(). With topology stability primed first, those
// auto-fired confirms must reach the champion and populate confirmedSupporters_.
// (The sibling test above primes AFTER the cascade and re-confirms explicitly;
// this one proves the cascade-driven confirm lands on its own.)
inline void chainMultiDeviceAutoConfirmReachesChampion(ChainMultiDeviceFixture* suite) {
    suite->spawnDevices(3);
    suite->setAllHunters();
    suite->connectLinearHunterChain();
    suite->deliverAllPackets();
    suite->syncAll();
    suite->deliverAllPackets();

    MultiDeviceNode& d0 = suite->node(0);
    MultiDeviceNode& d1 = suite->node(1);
    MultiDeviceNode& d2 = suite->node(2);

    // Prime stability BEFORE the cascade so the champion's confirm gate is open
    // when the auto-fired confirms arrive.
    suite->primeTopologyStableAll();

    // championMac_ resolving on d1/d2 triggers sendConfirm() internally; we
    // never call sendConfirm() ourselves.
    d0.chainManager->onChainStateChanged();
    d1.chainManager->onChainStateChanged();
    d2.chainManager->onChainStateChanged();
    suite->deliverAllPackets();
    // Re-prime + deliver so any confirm dropped during the cascade window is
    // retransmitted by the resender (driven by shootout->sync in syncAll) and
    // lands now that topology is settled.
    suite->primeTopologyStableAll();
    suite->deliverAllPackets();

    ASSERT_TRUE(d0.chainManager->isChampion());
    auto champ2 = d2.chainManager->getChampionMac();
    ASSERT_TRUE(champ2.has_value());
    ASSERT_EQ(memcmp(champ2->data(), d0.mac, 6), 0);

    // No explicit sendConfirm() was ever called; both supporters' auto-confirms
    // must have reached the champion.
    EXPECT_EQ(d0.chainManager->getConfirmedSupporterCount(), 2u);
    EXPECT_EQ(d0.chainManager->getBoostMs(), 2 * ChainManager::BOOST_PER_SUPPORTER_MS);
}

// End-to-end Shootout consensus: 4 devices form a ring, each confirms,
// every device reaches BRACKET_REVEAL phase, coordinator broadcasts
// MATCH_START and both duelists transition into MATCH_IN_PROGRESS.
inline void shootoutFourDeviceConsensusAndMatchStart(ChainMultiDeviceFixture* suite) {
    suite->spawnDevices(4);
    suite->setAllHunters();
    suite->connectLinearHunterChain();
    suite->deliverAllPackets();
    suite->syncAll();
    suite->deliverAllPackets();
    suite->closeRing();
    // Let the peer-graph converge so isInLoop() + isTopologyStable() agree,
    // and the 1Hz coord-derivation cycle fires the claim.
    suite->primeTopologyStableAll();

    int coordCount = 0;
    for (size_t i = 0; i < suite->nodeCount(); ++i) {
        if (suite->node(i).chainManager->isCoordinator()) coordCount++;
    }
    ASSERT_EQ(coordCount, 1) << "expected exactly one coordinator after ring close";

    // In the real flow, ShootoutProposal::onStateMounted calls startProposal
    // when the Idle→ShootoutProposal transition fires. Simulate that here
    // before dispatching button presses (confirmLocal).
    for (size_t i = 0; i < suite->nodeCount(); ++i) {
        suite->node(i).shootout->startProposal();
    }

    // After all confirms propagate every device reaches BRACKET_REVEAL; the
    // coordinator generates and broadcasts the bracket.
    for (size_t i = 0; i < suite->nodeCount(); ++i) {
        suite->node(i).shootout->confirmLocal();
        suite->deliverAllPackets();
    }
    suite->deliverAllPackets();
    suite->syncAll();
    suite->deliverAllPackets();

    for (size_t i = 0; i < suite->nodeCount(); ++i) {
        EXPECT_EQ(suite->node(i).shootout->getPhase(),
                  ShootoutManager::Phase::BRACKET_REVEAL)
            << "node " << i << " phase";
    }

    // Advance past the bracket-reveal window and let the coordinator fire
    // the first MATCH_START.
    suite->advanceClock(ShootoutManager::kBracketRevealMs + 100);
    suite->syncAll();
    suite->deliverAllPackets();
    suite->syncAll();
    suite->deliverAllPackets();

    // Receiving MATCH_START sets phase on everyone, so all nodes (coordinator,
    // both duelists, spectators) reach MATCH_IN_PROGRESS.
    int inProgress = 0;
    int localDuelists = 0;
    for (size_t i = 0; i < suite->nodeCount(); ++i) {
        if (suite->node(i).shootout->getPhase() == ShootoutManager::Phase::MATCH_IN_PROGRESS) {
            inProgress++;
        }
        if (suite->node(i).shootout->isLocalDuelist()) localDuelists++;
    }
    EXPECT_EQ(inProgress, static_cast<int>(suite->nodeCount()));
    EXPECT_EQ(localDuelists, 2);
}

// Drives a full n-device single-elimination tournament from ring closure
// through TOURNAMENT_END, asserting exactly n-1 matches, every node in ENDED,
// and winner consensus. postSpawn (optional) runs after spawn and before any
// traffic, for per-test seeding (e.g. player names).
inline void runFullTournament(ChainMultiDeviceFixture* suite, size_t n,
                              const std::function<void()>& postSpawn = nullptr) {
    suite->spawnDevices(n);
    suite->setAllHunters();
    if (postSpawn) postSpawn();
    suite->connectLinearHunterChain();
    suite->deliverAllPackets();
    suite->syncAll();
    suite->deliverAllPackets();
    suite->closeRing();
    suite->primeTopologyStableAll();

    for (size_t i = 0; i < suite->nodeCount(); ++i) {
        suite->node(i).shootout->startProposal();
    }
    for (size_t i = 0; i < suite->nodeCount(); ++i) {
        suite->node(i).shootout->confirmLocal();
        suite->deliverAllPackets();
    }
    suite->deliverAllPackets();
    suite->syncAll();
    suite->deliverAllPackets();

    suite->advanceClock(ShootoutManager::kBracketRevealMs + 100);
    suite->syncAll();
    suite->deliverAllPackets();
    suite->syncAll();
    suite->deliverAllPackets();

    const int expectedMatches = static_cast<int>(n) - 1;
    int matches = 0;
    for (int m = 0; m < 2 * expectedMatches; ++m) {
        int duelistIdx = -1;
        for (size_t i = 0; i < suite->nodeCount(); ++i) {
            if (suite->node(i).shootout->isLocalDuelist()) { duelistIdx = (int)i; break; }
        }
        if (duelistIdx < 0) break;
        // The opponent-wins flow is symmetric; picking one side keeps the
        // test deterministic.
        suite->node(duelistIdx).shootout->reportLocalWin();
        suite->deliverAllPackets();
        suite->syncAll();
        suite->deliverAllPackets();
        suite->syncAll();
        suite->deliverAllPackets();
        matches++;
        bool anyEnded = false;
        for (size_t i = 0; i < suite->nodeCount(); ++i) {
            if (suite->node(i).shootout->getPhase() == ShootoutManager::Phase::ENDED) {
                anyEnded = true;
                break;
            }
        }
        if (anyEnded) break;
    }

    EXPECT_EQ(matches, expectedMatches)
        << n << "-device SE bracket should run exactly " << expectedMatches << " matches";
    // All nodes end in ENDED phase with the same winner.
    std::array<uint8_t, 6> winner{};
    bool winnerSet = false;
    for (size_t i = 0; i < suite->nodeCount(); ++i) {
        EXPECT_EQ(suite->node(i).shootout->getPhase(), ShootoutManager::Phase::ENDED)
            << n << "-device node " << i << " phase not ENDED";
        auto wArr = suite->node(i).shootout->getTournamentWinner();
        if (!winnerSet) { winner = wArr; winnerSet = true; }
        else {
            EXPECT_EQ(memcmp(winner.data(), wArr.data(), 6), 0)
                << n << "-device node " << i << " disagrees on winner";
        }
    }
}

// 4 devices: 2 round-1 matches + 1 final. Guards against a round advancing
// after only 1 match or stalling between matches.
inline void shootoutFourDeviceFullTournament(ChainMultiDeviceFixture* suite) {
    runFullTournament(suite, 4);
}

// 8 devices: 4+2+1 matches across 3 rounds; catches off-by-one errors in
// advance-round that only show up beyond the first round. Also validates
// player-name propagation: every node sets a distinct name, and after the
// tournament each node resolves at least its own (direct peers resolve via
// cable CONFIRM; distant peers race BRACKET_REVEAL advancement).
inline void shootoutEightDeviceFullTournament(ChainMultiDeviceFixture* suite) {
    static const char* kNames[8] = {
        "alice", "bob", "carol", "dave", "erin", "frank", "gina", "hank"
    };
    runFullTournament(suite, 8, [suite] {
        for (size_t i = 0; i < suite->nodeCount(); ++i) {
            suite->node(i).player->setName(kNames[i]);
        }
    });
    for (size_t observer = 0; observer < suite->nodeCount(); ++observer) {
        std::string self = suite->node(observer).shootout->getNameForMac(
            suite->node(observer).mac);
        EXPECT_EQ(self, kNames[observer]) << "node " << observer << " self-name";
    }
}

// 16 devices: 8+4+2+1 matches across 4 rounds. 16 is exactly
// MAX_SHOOTOUT_MEMBERS, so this also exercises the boundary where the bracket
// fills the ESP-NOW peer cap without tripping the oversized-abort.
inline void shootoutSixteenDeviceFullTournament(ChainMultiDeviceFixture* suite) {
    runFullTournament(suite, 16);
}

// 3-device linear chain: after topology convergence every node must report
// isInLoop()==false.
inline void rdcThreeDeviceChainNoLoop(ChainMultiDeviceFixture* suite) {
    suite->spawnDevices(3);
    suite->setAllHunters();

    suite->connectLinearHunterChain();
    suite->advanceClock(2000);

    for (size_t i = 0; i < suite->nodeCount(); ++i) {
        EXPECT_FALSE(suite->node(i).rdc->isInLoop())
            << "Linear chain node " << i << " reported isInLoop()==true";
    }
}

// 3-device hunter ring: after closing the ring and letting the peer-graph
// converge, exactly one ChainManager reports isCoordinator()=true. The graph-
// derived election picks the lowest-MAC member of the loop; with sequential
// MACs 0x10/0x11/0x12 in spawnDevices, node 0 wins.
inline void chainHunterRingClaimsExactlyOneCoordinator(ChainMultiDeviceFixture* suite) {
    suite->spawnDevices(3);
    suite->setAllHunters();

    suite->connectLinearHunterChain();
    suite->deliverAllPackets();
    suite->syncAll();
    suite->deliverAllPackets();
    suite->closeRing();

    suite->primeTopologyStableAll();

    // Every node should agree the chain is in a 3-member loop.
    for (size_t i = 0; i < suite->nodeCount(); ++i) {
        EXPECT_TRUE(suite->node(i).rdc->isInLoop())
            << "node " << i << " did not observe loop";
        EXPECT_TRUE(suite->node(i).rdc->isTopologyStable())
            << "node " << i << " topology did not stabilize";
        EXPECT_EQ(suite->node(i).rdc->getChainMembers().size(), 3u)
            << "node " << i << " chain members";
    }

    int coordCount = 0;
    for (size_t i = 0; i < suite->nodeCount(); ++i) {
        if (suite->node(i).chainManager->isCoordinator()) {
            coordCount++;
        }
    }
    EXPECT_EQ(coordCount, 1)
        << "Expected exactly one coordinator in 3-hunter ring (got " << coordCount << ")";
}

// A closed ring has no chain roles. Because champion is derived from the same
// loop-detection the coordinator uses (championMac is nullopt inside a loop),
// every member of an all-hunter ring reports neither champion nor supporter;
// they are all in the shootout.
inline void chainCoordinatorIsNeverSupporter(ChainMultiDeviceFixture* suite) {
    suite->spawnDevices(3);
    suite->setAllHunters();
    suite->connectLinearHunterChain();
    suite->deliverAllPackets();
    suite->syncAll();
    suite->deliverAllPackets();
    suite->closeRing();
    suite->primeTopologyStableAll();

    int coordCount = 0;
    for (size_t i = 0; i < suite->nodeCount(); ++i) {
        ASSERT_TRUE(suite->node(i).rdc->isInLoop()) << "node " << i << " not in loop";
        if (suite->node(i).chainManager->isCoordinator()) coordCount++;
        EXPECT_FALSE(suite->node(i).chainManager->isSupporter())
            << "ring member " << i << " must not be a chain supporter";
        EXPECT_FALSE(suite->node(i).chainManager->isChampion())
            << "ring member " << i << " must not be a champion";
    }
    EXPECT_EQ(coordCount, 1) << "closed ring elects exactly one coordinator";
}

// Mixed-role ring (H1-H2-B1 closed): the peer-graph is role-agnostic, so
// after the closing cable plugs in and the graph converges, exactly one
// ChainManager, the lowest-MAC member of the loop, reports isCoordinator()
// regardless of role mix. Regression case for election across the
// hunter/bounty boundary.
inline void chainMixedRoleRingClaimsCoordinator(ChainMultiDeviceFixture* suite) {
    suite->spawnDevices(3);
    suite->node(0).player->setIsHunter(true);   // H1
    suite->node(1).player->setIsHunter(true);   // H2
    suite->node(2).player->setIsHunter(false);  // B1

    // Build linear chain: H2.OUTPUT ↔ H1.INPUT (same-role), then
    // B1.OUTPUT ↔ H2.INPUT (different-role at the chain seam).
    suite->connectOutputToPrev(1);
    suite->connectOutputToPrev(2);
    suite->deliverAllPackets();
    suite->syncAll();
    suite->deliverAllPackets();

    suite->closeRing();

    suite->primeTopologyStableAll();

    // All three devices observe the loop and a 3-member chain.
    for (size_t i = 0; i < suite->nodeCount(); ++i) {
        EXPECT_TRUE(suite->node(i).rdc->isInLoop())
            << "node " << i << " did not observe loop";
        EXPECT_EQ(suite->node(i).rdc->getChainMembers().size(), 3u)
            << "node " << i << " chain members";
    }

    int coordCount = 0;
    for (size_t i = 0; i < suite->nodeCount(); ++i) {
        if (suite->node(i).chainManager->isCoordinator()) {
            coordCount++;
        }
    }
    EXPECT_EQ(coordCount, 1)
        << "Mixed-role ring did not elect exactly one coordinator (got "
        << coordCount << "). This is the regression case.";
}

// 3-device same-role ring: after the closing cable plugs in and the topology
// converges, every device must report isInLoop()=true. Pairs with
// rdcMixedRoleRingReportsLoop to cover both homogeneous and mixed roles.
inline void rdcThreeDeviceRingReportsLoop(ChainMultiDeviceFixture* suite) {
    suite->spawnDevices(3);
    suite->setAllHunters();

    // Phase 1: linear chain. After convergence no device observes a loop.
    suite->connectLinearHunterChain();
    suite->primeTopologyStableAll();
    for (size_t i = 0; i < suite->nodeCount(); ++i) {
        EXPECT_FALSE(suite->node(i).rdc->isInLoop())
            << "linear-phase node " << i << " spuriously reported loop";
    }

    // Phase 2: ring close.
    suite->closeRing();
    suite->primeTopologyStableAll();
    for (size_t i = 0; i < suite->nodeCount(); ++i) {
        EXPECT_TRUE(suite->node(i).rdc->isInLoop())
            << "ring node " << i << " did not detect loop";
        EXPECT_EQ(suite->node(i).rdc->getChainMembers().size(), 3u)
            << "node " << i << " chain members";
    }
}

// Back-to-back tournaments on one physically-closed 4-ring: run a full
// tournament to ENDED, resetToIdle every node (mirrors
// FinalStandings::onStateDismounted), then run a second. Guards that the
// coordinator claim survives the reset (exactly one coordinator with the full
// 4-member loop post-reset) and that a second tournament re-stabilizes and runs
// its 3 matches without a fresh ring-close event.
inline void shootoutFourDeviceTwoTournamentsBackToBack(ChainMultiDeviceFixture* suite) {
    suite->spawnDevices(4);
    suite->setAllHunters();
    suite->connectLinearHunterChain();
    suite->deliverAllPackets();
    suite->syncAll();
    suite->deliverAllPackets();
    suite->closeRing();

    auto runOne = [&]() {
        // Let topology stability re-derive over BEACON cycles before proposing.
        // In firmware this is the time a device spends back in Idle before the
        // Idle->ShootoutProposal edge re-fires; tournament 2 must re-stabilize
        // just like tournament 1.
        suite->primeTopologyStableAll();
        for (size_t i = 0; i < suite->nodeCount(); ++i) {
            suite->node(i).shootout->startProposal();
        }
        for (size_t i = 0; i < suite->nodeCount(); ++i) {
            suite->node(i).shootout->confirmLocal();
            suite->deliverAllPackets();
        }
        suite->deliverAllPackets();
        suite->syncAll();
        suite->deliverAllPackets();
        suite->advanceClock(ShootoutManager::kBracketRevealMs + 100);
        suite->syncAll();
        suite->deliverAllPackets();
        suite->syncAll();
        suite->deliverAllPackets();

        int matches = 0;
        for (int m = 0; m < 6; ++m) {
            int duelistIdx = -1;
            for (size_t i = 0; i < suite->nodeCount(); ++i) {
                if (suite->node(i).shootout->isLocalDuelist()) { duelistIdx = (int)i; break; }
            }
            if (duelistIdx < 0) break;
            suite->node(duelistIdx).shootout->reportLocalWin();
            suite->deliverAllPackets();
            suite->syncAll();
            suite->deliverAllPackets();
            suite->syncAll();
            suite->deliverAllPackets();
            matches++;
            bool anyEnded = false;
            for (size_t i = 0; i < suite->nodeCount(); ++i) {
                if (suite->node(i).shootout->getPhase() == ShootoutManager::Phase::ENDED) {
                    anyEnded = true; break;
                }
            }
            if (anyEnded) break;
        }
        return matches;
    };

    int matchesOne = runOne();
    EXPECT_EQ(matchesOne, 3);
    for (size_t i = 0; i < suite->nodeCount(); ++i) {
        EXPECT_EQ(suite->node(i).shootout->getPhase(), ShootoutManager::Phase::ENDED)
            << "tourn 1 node " << i;
    }

    // Reset on every node (mirrors FinalStandings::onStateDismounted).
    for (size_t i = 0; i < suite->nodeCount(); ++i) {
        suite->node(i).shootout->resetToIdle();
        EXPECT_EQ(suite->node(i).shootout->getPhase(), ShootoutManager::Phase::IDLE)
            << "reset node " << i;
    }

    int coordCount = 0;
    for (size_t i = 0; i < suite->nodeCount(); ++i) {
        if (suite->node(i).chainManager->isCoordinator()) {
            coordCount++;
            ASSERT_EQ(suite->node(i).shootout->getLoopMembers().size(), 4u)
                << "loop members wrong on coordinator node " << i;
        }
    }
    ASSERT_EQ(coordCount, 1) << "expected exactly one coordinator post-reset";

    int matchesTwo = runOne();
    EXPECT_EQ(matchesTwo, 3) << "tournament 2 did not run 3 matches";
    for (size_t i = 0; i < suite->nodeCount(); ++i) {
        EXPECT_EQ(suite->node(i).shootout->getPhase(), ShootoutManager::Phase::ENDED)
            << "tourn 2 node " << i;
    }
}

// 6-device mixed loop, head-to-head cable plugged first. After both closing
// cables converge, exactly one node, the lowest-MAC member of the 6-loop,
// is coordinator and its loop view contains all 6.
inline void chainSixDeviceMixedLoopHeadToHeadFirst(ChainMultiDeviceFixture* suite) {
    suite->spawnDevices(6);
    suite->node(0).player->setIsHunter(true);
    suite->node(1).player->setIsHunter(true);
    suite->node(2).player->setIsHunter(true);
    suite->node(3).player->setIsHunter(false);
    suite->node(4).player->setIsHunter(false);
    suite->node(5).player->setIsHunter(false);

    suite->connectOutputToPrev(1);
    suite->connectOutputToPrev(2);
    suite->connectOutputToPrev(4);
    suite->connectOutputToPrev(5);
    suite->deliverAllPackets();
    suite->syncAll();
    suite->deliverAllPackets();

    // PHASE A: head-to-head cable first.
    suite->closeRing();
    suite->deliverAllPackets();
    suite->syncAll();
    suite->deliverAllPackets();

    // PHASE B: tail-to-tail closer completes the loop.
    suite->connectTailToTail(2, 3);
    suite->primeTopologyStableAll();

    int coordCount = 0;
    for (size_t i = 0; i < suite->nodeCount(); ++i) {
        EXPECT_TRUE(suite->node(i).rdc->isInLoop())
            << "node " << i << " did not observe loop";
        EXPECT_EQ(suite->node(i).rdc->getChainMembers().size(), 6u)
            << "node " << i << " chain members";
        if (suite->node(i).chainManager->isCoordinator()) coordCount++;
    }
    EXPECT_EQ(coordCount, 1) << "6-device mixed loop should elect exactly one coordinator";

    for (size_t i = 0; i < suite->nodeCount(); ++i) {
        if (suite->node(i).chainManager->isCoordinator()) {
            auto members = suite->node(i).shootout->getLoopMembers();
            EXPECT_EQ(members.size(), 6u)
                << "Coord at node " << i << ": expected 6, got " << members.size();
        }
    }
}

// 6-device mixed loop assembled tail-to-tail first. After both chains are
// built, the AUX-to-AUX cable joins them (no duel triggered). The head-to-head
// closer triggers loop closure on both sides; under the peer-graph, all six
// devices converge to the same 6-member set and the lowest-MAC node
// claims coordinator after the 1-cycle stability window.
//
// Wiring layout after connectOutputToPrev(1), (2), (4), (5):
//   node 0 (H coord): OUTPUT free (hunter opponent jack)
//   node 1 (H mid):   both used
//   node 2 (H tail):  INPUT free  ─── tail-to-tail cable ───> node 3 OUTPUT
//   node 3 (B tail):  OUTPUT free <─── (same cable)
//   node 4 (B mid):   both used
//   node 5 (B coord): INPUT free  (bounty opponent jack)
//
// Phase A: connectTailToTail(2, 3), AUX-to-AUX.
// Phase B: closeRing()             (node[0].OUTPUT ↔ node[5].INPUT.
// Order-independent: after both cables and topology convergence, exactly one
// coordinator with a 6-member loop view.
inline void chainSixDeviceMixedLoopTailToTailFirst(ChainMultiDeviceFixture* suite) {
    suite->spawnDevices(6);
    suite->node(0).player->setIsHunter(true);
    suite->node(1).player->setIsHunter(true);
    suite->node(2).player->setIsHunter(true);
    suite->node(3).player->setIsHunter(false);
    suite->node(4).player->setIsHunter(false);
    suite->node(5).player->setIsHunter(false);

    // Build hunter chain (node 0–2) and bounty chain (node 3–5).
    suite->connectOutputToPrev(1);
    suite->connectOutputToPrev(2);
    suite->connectOutputToPrev(4);
    suite->connectOutputToPrev(5);
    suite->deliverAllPackets();
    suite->syncAll();
    suite->deliverAllPackets();

    // PHASE A: tail-to-tail cable joins chains.
    suite->connectTailToTail(2, 3);
    suite->deliverAllPackets();
    suite->syncAll();
    suite->deliverAllPackets();

    // PHASE B: head-to-head closer completes the loop.
    suite->closeRing();
    suite->primeTopologyStableAll();

    int coordCount = 0;
    for (size_t i = 0; i < suite->nodeCount(); ++i) {
        EXPECT_TRUE(suite->node(i).rdc->isInLoop())
            << "node " << i << " did not observe loop";
        EXPECT_EQ(suite->node(i).rdc->getChainMembers().size(), 6u)
            << "node " << i << " chain members";
        if (suite->node(i).chainManager->isCoordinator()) coordCount++;
    }
    EXPECT_EQ(coordCount, 1) << "6-device mixed loop should elect exactly one coordinator";

    for (size_t i = 0; i < suite->nodeCount(); ++i) {
        if (suite->node(i).chainManager->isCoordinator()) {
            auto members = suite->node(i).shootout->getLoopMembers();
            EXPECT_EQ(members.size(), 6u)
                << "Coord at node " << i << ": expected 6 members, got "
                << members.size();
        }
    }
}

// 4-device HHBB mixed loop, tail-to-tail first. Under the peer-graph
// protocol, role mix is irrelevant for election; the lowest-MAC member of the
// 4-device loop is the single coordinator and its loop view contains all 4.
inline void chainFourDeviceMixedLoopTailToTailFirst(ChainMultiDeviceFixture* suite) {
    suite->spawnDevices(4);
    suite->node(0).player->setIsHunter(true);
    suite->node(1).player->setIsHunter(true);
    suite->node(2).player->setIsHunter(false);
    suite->node(3).player->setIsHunter(false);

    suite->connectOutputToPrev(1);  // hunter chain: node[1].OUTPUT <-> node[0].INPUT
    suite->connectOutputToPrev(3);  // bounty chain: node[3].OUTPUT <-> node[2].INPUT
    suite->deliverAllPackets();
    suite->syncAll();
    suite->deliverAllPackets();

    // PHASE A: tail-to-tail cable joins chains.
    suite->connectTailToTail(1, 2);
    suite->deliverAllPackets();
    suite->syncAll();
    suite->deliverAllPackets();

    // PHASE B: head-to-head closer.
    suite->closeRing();
    suite->primeTopologyStableAll();

    int coordCount = 0;
    for (size_t i = 0; i < suite->nodeCount(); ++i) {
        EXPECT_TRUE(suite->node(i).rdc->isInLoop())
            << "node " << i << " did not observe loop";
        EXPECT_EQ(suite->node(i).rdc->getChainMembers().size(), 4u)
            << "node " << i << " chain members";
        if (suite->node(i).chainManager->isCoordinator()) coordCount++;
    }
    EXPECT_EQ(coordCount, 1) << "4-device mixed loop should elect exactly one coordinator";

    for (size_t i = 0; i < suite->nodeCount(); ++i) {
        if (suite->node(i).chainManager->isCoordinator()) {
            auto members = suite->node(i).shootout->getLoopMembers();
            EXPECT_EQ(members.size(), 4u)
                << "Coord at node " << i << ": expected 4, got " << members.size();
        }
    }
}

// After a 4-device mixed loop has converged and a coordinator claimed, the
// onDirectPeerChange disconnect on the coordinator's loop-edge jack must
// immediately demote it (without waiting for the next 1Hz derivation tick).
inline void chainMixedLoopCableYankClearsLoopMergeState(ChainMultiDeviceFixture* suite) {
    suite->spawnDevices(4);
    suite->node(0).player->setIsHunter(true);
    suite->node(1).player->setIsHunter(true);
    suite->node(2).player->setIsHunter(false);
    suite->node(3).player->setIsHunter(false);

    suite->connectOutputToPrev(1);
    suite->connectOutputToPrev(3);
    suite->deliverAllPackets();
    suite->syncAll();
    suite->deliverAllPackets();

    // PHASE A: tail-to-tail cable.
    suite->connectTailToTail(1, 2);
    suite->deliverAllPackets();
    suite->syncAll();
    suite->deliverAllPackets();

    // PHASE B: head-to-head closer.
    suite->closeRing();
    suite->primeTopologyStableAll();

    // Locate the elected coordinator (lowest MAC of the 4-loop).
    int coordIdx = -1;
    for (size_t i = 0; i < suite->nodeCount(); ++i) {
        if (suite->node(i).chainManager->isCoordinator()) {
            ASSERT_EQ(coordIdx, -1) << "more than one coordinator claimed";
            coordIdx = static_cast<int>(i);
        }
    }
    ASSERT_GE(coordIdx, 0) << "no coordinator claimed after ring close";

    // YANK: fire an onDirectPeerChange disconnect on the coord's OUTPUT jack
    // (closeRing connects node[0].OUTPUT ↔ node[N-1].INPUT). The
    // current=nullopt branch demotes immediately; prev carries the peer for
    // bookkeeping but is unused on the disconnect path.
    RemoteDeviceCoordinator::Peer prevPeer;
    prevPeer.mac.fill(0);
    prevPeer.deviceType = DeviceType::PDN;
    suite->node(coordIdx).chainManager->onDirectPeerChange(
        SerialIdentifier::OUTPUT_JACK,
        std::optional<RemoteDeviceCoordinator::Peer>(prevPeer),
        std::nullopt);

    EXPECT_FALSE(suite->node(coordIdx).chainManager->isCoordinator())
        << "coordinator did not demote on cable yank";
}

// 16-device ring at the design ceiling (MAX_SHOOTOUT_MEMBERS). Alternating
// roles maximize role boundaries; all 16 must appear in every node's chain
// members after convergence.
inline void chainSixteenDeviceRingFullBracket(ChainMultiDeviceFixture* suite) {
    suite->spawnDevices(16);
    for (int i = 0; i < 16; ++i) {
        suite->node(i).player->setIsHunter((i % 2) == 0);
    }
    for (int i = 1; i < 16; ++i) {
        suite->connectOutputToPrev(static_cast<size_t>(i));
    }
    suite->deliverAllPackets();
    suite->syncAll();
    suite->deliverAllPackets();

    suite->closeRing();
    suite->primeTopologyStableAll();

    for (size_t i = 0; i < suite->nodeCount(); ++i) {
        EXPECT_EQ(suite->node(i).rdc->getChainMembers().size(), 16u)
            << "16-ring node " << i;
        EXPECT_TRUE(suite->node(i).rdc->isInLoop())
            << "16-ring node " << i;
    }
}

// 4-device HHBB mixed loop, head-to-head first. Same final topology as
// tail-to-tail-first; the peer-graph protocol is order-independent and
// converges to exactly one coordinator with a 4-member loop view.
inline void chainFourDeviceMixedLoopHeadToHeadFirst(ChainMultiDeviceFixture* suite) {
    suite->spawnDevices(4);
    suite->node(0).player->setIsHunter(true);
    suite->node(1).player->setIsHunter(true);
    suite->node(2).player->setIsHunter(false);
    suite->node(3).player->setIsHunter(false);

    suite->connectOutputToPrev(1);
    suite->connectOutputToPrev(3);
    suite->deliverAllPackets();
    suite->syncAll();
    suite->deliverAllPackets();

    // PHASE A: head-to-head first.
    suite->closeRing();
    suite->deliverAllPackets();
    suite->syncAll();
    suite->deliverAllPackets();

    // PHASE B: tail-to-tail closer completes the loop.
    suite->connectTailToTail(1, 2);
    suite->primeTopologyStableAll();

    int coordCount = 0;
    for (size_t i = 0; i < suite->nodeCount(); ++i) {
        EXPECT_TRUE(suite->node(i).rdc->isInLoop())
            << "node " << i << " did not observe loop";
        EXPECT_EQ(suite->node(i).rdc->getChainMembers().size(), 4u)
            << "node " << i << " chain members";
        if (suite->node(i).chainManager->isCoordinator()) coordCount++;
    }
    EXPECT_EQ(coordCount, 1) << "4-device mixed loop should elect exactly one coordinator";

    for (size_t i = 0; i < suite->nodeCount(); ++i) {
        if (suite->node(i).chainManager->isCoordinator()) {
            auto members = suite->node(i).shootout->getLoopMembers();
            EXPECT_EQ(members.size(), 4u);
        }
    }
}

// Cable-yank-mid-ring convergence test. Build a converged N-device ring, snap
// one cable, and assert isInLoop() returns false on every node. The edge break
// propagates one hop per sync as each node floods an updated BEACON, so the loop
// collapses within ~N/2 hops, fast enough for human-perception responsive
// loop-break feedback.
inline void cableYankConvergesRingToLinear(ChainMultiDeviceFixture* suite,
                                           size_t ringSize) {
    suite->spawnDevices(ringSize);
    for (size_t i = 0; i < ringSize; ++i) {
        suite->node(i).player->setIsHunter((i % 2) == 0);
    }
    for (size_t i = 1; i < ringSize; ++i) {
        suite->connectOutputToPrev(i);
    }
    suite->deliverAllPackets();
    suite->syncAll();
    suite->deliverAllPackets();
    suite->closeRing();
    suite->primeTopologyStableAll();

    for (size_t i = 0; i < ringSize; ++i) {
        ASSERT_TRUE(suite->node(i).rdc->isInLoop()) << "ring node " << i;
    }

    // Snap a mid-ring cable (between node[N/2 - 1].INPUT_JACK and
    // node[N/2].OUTPUT_JACK, the standard connectOutputToPrev pairing).
    const size_t snapIdx = ringSize / 2;
    suite->breakSerialLink(snapIdx, SerialIdentifier::OUTPUT_JACK,
                           snapIdx - 1, SerialIdentifier::INPUT_JACK);

    // One BEACON hop propagates per sync, so tick at 100ms and pump packets
    // each tick. Budget = N/2 hops + the 1Hz backstop settle cycle; 2N ticks of
    // 100ms (= 0.2N seconds) gives comfortable slack.
    const int ticks = static_cast<int>(ringSize * 2) + 20;
    for (int t = 0; t < ticks; ++t) {
        suite->advanceClock(100);
        suite->syncAll();
        suite->deliverAllPackets();
    }

    for (size_t i = 0; i < ringSize; ++i) {
        EXPECT_FALSE(suite->node(i).rdc->isInLoop())
            << "ring-to-linear convergence failed at node " << i
            << " for ringSize=" << ringSize;
    }
}

inline void chainCableYankSixteenRingConverges(ChainMultiDeviceFixture* suite) {
    cableYankConvergesRingToLinear(suite, 16);
}

inline void chainCableYankFiftyRingConverges(ChainMultiDeviceFixture* suite) {
    cableYankConvergesRingToLinear(suite, 50);
}

// With no topology change, BEACON traffic is just the 1Hz backstop
// (kBeaconBackstopMs): self plus one replayed beacon per other chain member
// (loss repair; receivers dedup so none of it re-forwards). The event-driven
// edge-change floods stay silent. Over ≥10 seconds of idle on a 2-node chain,
// framesTx must rise at ~2 frames/s, never faster; catches any spurious
// re-emission.
inline void chainIdleCadenceSettlesToOneHz(ChainMultiDeviceFixture* suite) {
    suite->spawnDevices(2);
    suite->setAllHunters();
    suite->connectOutputToPrev(1);
    suite->deliverAllPackets();
    suite->syncAll();
    suite->deliverAllPackets();
    suite->primeTopologyStableAll();

    const auto baseOut = suite->node(0).rdc->getBeaconStats(
        SerialIdentifier::OUTPUT_JACK).framesTx;
    const auto baseIn = suite->node(1).rdc->getBeaconStats(
        SerialIdentifier::INPUT_JACK).framesTx;

    for (int sec = 0; sec < 10; ++sec) {
        for (int sub = 0; sub < 10; ++sub) {
            suite->advanceClock(100);
            suite->syncAll();
            suite->deliverAllPackets();
        }
    }

    const auto endOut = suite->node(0).rdc->getBeaconStats(
        SerialIdentifier::OUTPUT_JACK).framesTx;
    const auto endIn = suite->node(1).rdc->getBeaconStats(
        SerialIdentifier::INPUT_JACK).framesTx;

    // 1Hz backstop over 10s = 10 ticks × (self + 1 member replay) = 20 BEACONs
    // per jack. The ceiling of 24 allows a boundary-tick slop but still fails a
    // 2Hz regression (~40) or any event-driven re-emission storm.
    EXPECT_LE(endOut - baseOut, 24u)
        << "idle node 0 OUTPUT did not settle to 1Hz BEACON cadence";
    EXPECT_LE(endIn - baseIn, 24u)
        << "idle node 1 INPUT did not settle to 1Hz BEACON cadence";
}

// ============================================================================
// Regression guards for chain-duel hardware bring-up. Each encodes a bug that
// otherwise only surfaced by plugging cables and reading logs.
// ============================================================================

// deviceType must survive the HELLO wire: getPeerDeviceType() must read back
// PDN, or the duel gate (which requires getPeerDeviceType(OUTPUT)==PDN) breaks.
inline void chainDeviceTypePropagatesViaHello(ChainMultiDeviceFixture* suite) {
    suite->spawnDevices(2);
    suite->setAllHunters();
    suite->connectOutputToPrev(1);  // node1.OUTPUT <-> node0.INPUT
    MultiDeviceNode& d0 = suite->node(0);
    MultiDeviceNode& d1 = suite->node(1);
    ASSERT_NE(d0.rdc->getPeerMac(SerialIdentifier::INPUT_JACK), nullptr);
    ASSERT_NE(d1.rdc->getPeerMac(SerialIdentifier::OUTPUT_JACK), nullptr);
    EXPECT_EQ(d0.rdc->getPeerDeviceType(SerialIdentifier::INPUT_JACK), DeviceType::PDN);
    EXPECT_EQ(d1.rdc->getPeerDeviceType(SerialIdentifier::OUTPUT_JACK), DeviceType::PDN);
}

// The peer's 4-digit id rides the HELLO through to getPeerUserId; a peer that
// has not registered yet (0xFFFF on the wire) reads back as no id.
inline void chainUserIdPropagatesViaHello(ChainMultiDeviceFixture* suite) {
    suite->spawnDevices(1);
    MultiDeviceNode& d0 = suite->node(0);
    EXPECT_FALSE(d0.rdc->getPeerUserId(SerialIdentifier::INPUT_JACK).has_value());

    uint8_t peerMac[6] = {0xAB, 0xCD, 0xEF, 0x01, 0x02, 0x03};
    suite->deliverHello(*d0.device, SerialIdentifier::INPUT_JACK, peerMac,
                        static_cast<uint8_t>(DeviceType::PDN), 4242);
    auto id = d0.rdc->getPeerUserId(SerialIdentifier::INPUT_JACK);
    ASSERT_TRUE(id.has_value());
    EXPECT_EQ(*id, 4242);

    // A peer present but not yet registered (0xFFFF) reads back as no id.
    uint8_t peerMac2[6] = {0x10, 0x20, 0x30, 0x40, 0x50, 0x60};
    suite->deliverHello(*d0.device, SerialIdentifier::INPUT_JACK, peerMac2,
                        static_cast<uint8_t>(DeviceType::PDN), peer_graph::kUserIdUnknown);
    ASSERT_NE(d0.rdc->getPeerMac(SerialIdentifier::INPUT_JACK), nullptr);
    EXPECT_FALSE(d0.rdc->getPeerUserId(SerialIdentifier::INPUT_JACK).has_value());
}

// Two devices cross-cabled on both jacks are a closed ring: the same peer on
// both jacks is a loop even though the graph has only one distinct neighbor.
inline void chainTwoDeviceBothJacksIsLoop(ChainMultiDeviceFixture* suite) {
    suite->spawnDevices(1);
    MultiDeviceNode& d0 = suite->node(0);
    uint8_t peer[6] = {0xAB, 0xCD, 0xEF, 0x01, 0x02, 0x03};

    suite->deliverHello(*d0.device, SerialIdentifier::INPUT_JACK, peer);
    EXPECT_FALSE(d0.rdc->isInLoop());  // one jack only: not a loop

    suite->deliverHello(*d0.device, SerialIdentifier::OUTPUT_JACK, peer);
    EXPECT_TRUE(d0.rdc->isInLoop());   // same peer on both jacks: two-device ring

    uint8_t other[6] = {0x10, 0x20, 0x30, 0x40, 0x50, 0x60};
    suite->deliverHello(*d0.device, SerialIdentifier::OUTPUT_JACK, other);
    EXPECT_FALSE(d0.rdc->isInLoop());  // different peers: open chain, not a loop
}

// A peer announcing FDN reads back as FDN, so the game routes it to the symbol
// interaction rather than a duel.
inline void chainFdnPeerReportsFdnDeviceType(ChainMultiDeviceFixture* suite) {
    suite->spawnDevices(1);
    suite->setAllHunters();
    MultiDeviceNode& d0 = suite->node(0);
    uint8_t peerMac[6] = {0x02, 0, 0, 0, 0, 0x77};
    suite->deliverHello(*d0.device, SerialIdentifier::OUTPUT_JACK, peerMac,
                        static_cast<uint8_t>(DeviceType::FDN));
    ASSERT_NE(d0.rdc->getPeerMac(SerialIdentifier::OUTPUT_JACK), nullptr);
    EXPECT_EQ(d0.rdc->getPeerDeviceType(SerialIdentifier::OUTPUT_JACK), DeviceType::FDN);
}

// A self-sourced HELLO (the GPIO-38 OUTPUT-jack echo) must NOT establish a peer.
// If it did, a yanked OUTPUT cable would never clear macPeer.
inline void chainSelfHelloEchoRejected(ChainMultiDeviceFixture* suite) {
    suite->spawnDevices(1);
    suite->setAllHunters();
    MultiDeviceNode& d0 = suite->node(0);
    suite->deliverHello(*d0.device, SerialIdentifier::OUTPUT_JACK, d0.mac);
    EXPECT_EQ(d0.rdc->getPeerMac(SerialIdentifier::OUTPUT_JACK), nullptr)
        << "self-sourced HELLO must not set macPeer";
}

// Champion's idle "Posse" = live supporter-side chain length from the peer-graph;
// non-champions report 0.
inline void chainChampionChainLengthFromTopology(ChainMultiDeviceFixture* suite) {
    suite->spawnDevices(3);
    suite->setAllHunters();
    suite->connectLinearHunterChain();  // d0 champion (OUTPUT free), d1/d2 behind
    suite->deliverAllPackets();
    suite->syncAll();
    suite->deliverAllPackets();
    MultiDeviceNode& d0 = suite->node(0);
    MultiDeviceNode& d1 = suite->node(1);
    for (int pass = 0; pass < 2; ++pass) {
        d0.chainManager->onChainStateChanged();
        d1.chainManager->onChainStateChanged();
        suite->node(2).chainManager->onChainStateChanged();
        suite->deliverAllPackets();
    }
    ASSERT_TRUE(d0.chainManager->isChampion());
    EXPECT_EQ(d0.chainManager->getChainLength(), 2u);  // two supporters behind the champion
    EXPECT_EQ(d1.chainManager->getChainLength(), 0u);  // non-champion reports 0
}

// Open mixed chain (the duel scenario we hand-tested): the two champions face
// each other and each has a supporter behind it. Validates role convergence
// (roles ride the BEACON flood; the 1Hz backstop re-sends kChainConfirm) and
// champion/supporter derivation across the role boundary.
//   B-supp(0) <- B-champ(1) <-> H-champ(2) <- H-supp(3)
inline void chainMixedChainRolesConverge(ChainMultiDeviceFixture* suite) {
    suite->spawnDevices(4);
    suite->node(0).player->setIsHunter(false);  // B-supp
    suite->node(1).player->setIsHunter(false);  // B-champ
    suite->node(2).player->setIsHunter(true);   // H-champ
    suite->node(3).player->setIsHunter(true);   // H-supp

    suite->connectOutputToPrev(1);  // B-champ.OUTPUT <-> B-supp.INPUT
    suite->connectOutputToPrev(2);  // H-champ.OUTPUT <-> B-champ.INPUT (duel link)
    suite->connectOutputToPrev(3);  // H-supp.OUTPUT  <-> H-champ.INPUT

    // Converge roles: the confirm backstop fires from chainManager->sync() at
    // 1Hz; interleave deliverAllPackets to route the ESP-NOW kChainConfirms,
    // and run onChainStateChanged (what Quickdraw drives each tick).
    for (int i = 0; i < 6; ++i) {
        suite->advanceClock(1100);
        suite->syncAll();
        suite->deliverAllPackets();
        for (size_t n = 0; n < suite->nodeCount(); ++n)
            suite->node(n).chainManager->onChainStateChanged();
        suite->deliverAllPackets();
    }

    EXPECT_TRUE(suite->node(1).chainManager->isChampion())  << "B-champ should be champion";
    EXPECT_TRUE(suite->node(2).chainManager->isChampion())  << "H-champ should be champion";
    EXPECT_TRUE(suite->node(0).chainManager->isSupporter()) << "B-supp should be supporter";
    EXPECT_TRUE(suite->node(3).chainManager->isSupporter()) << "H-supp should be supporter";
}

// A closed hunter ring that loses a link becomes an open chain with exactly one
// champion (the OUTPUT tail), not all-supporters.
inline void chainRingYankLeavesOneChampion(ChainMultiDeviceFixture* suite) {
    suite->spawnDevices(4);
    suite->setAllHunters();
    suite->connectLinearHunterChain();
    suite->deliverAllPackets();
    suite->syncAll();
    suite->deliverAllPackets();
    suite->closeRing();
    suite->primeTopologyStableAll();
    ASSERT_TRUE(suite->node(0).rdc->isInLoop()) << "precondition: closed ring";

    // Yank the closing link (head OUTPUT <-> tail INPUT).
    size_t tail = suite->nodeCount() - 1;
    suite->breakSerialLink(0, SerialIdentifier::OUTPUT_JACK,
                           tail, SerialIdentifier::INPUT_JACK);
    suite->primeTopologyStableAll();

    int champions = 0;
    for (size_t i = 0; i < suite->nodeCount(); ++i) {
        EXPECT_FALSE(suite->node(i).rdc->isInLoop()) << "node " << i << " still loop";
        if (suite->node(i).chainManager->isChampion()) champions++;
    }
    EXPECT_EQ(champions, 1) << "open hunter chain must have exactly one champion";
}

// Mutual-consistency: a one-sided HELLO (we hear them, they don't hear us) must
// NOT produce a graph edge: no phantom member, no phantom loop.
inline void chainHalfOpenLinkNoPhantomEdge(ChainMultiDeviceFixture* suite) {
    suite->spawnDevices(2);
    suite->setAllHunters();
    MultiDeviceNode& d0 = suite->node(0);
    suite->deliverHello(*d0.device, SerialIdentifier::INPUT_JACK, suite->node(1).mac);
    d0.rdc->sync();
    EXPECT_EQ(d0.rdc->getChainMembers().size(), 1u)
        << "one-sided HELLO must not form a mutual edge";
    EXPECT_FALSE(d0.rdc->isInLoop());
}

// Half-open RING regression. Four hunters wired in a chain, then the closing
// cable is plugged but conducts only one direction: node0 hears the tail on its
// OUTPUT jack, the tail never hears node0. The mutual-edge rule rejects the
// half-open link, so no loop forms, and because role now rides the same BEACON
// flood as topology, the chain must resolve to exactly one champion instead of
// stranding all four as supporters.
inline void chainHalfOpenRingResolvesChampion(ChainMultiDeviceFixture* suite) {
    suite->spawnDevices(4);
    suite->setAllHunters();
    suite->connectLinearHunterChain();
    suite->deliverAllPackets();
    suite->syncAll();
    suite->deliverAllPackets();

    // One-directional closing HELLO: node0.OUTPUT learns the tail, but the tail's
    // INPUT never learns node0 (no closing serial link is recorded, so the tail's
    // beacon keeps INPUT open). The edge is therefore non-mutual.
    size_t tail = suite->nodeCount() - 1;
    suite->deliverHello(*suite->node(0).device, SerialIdentifier::OUTPUT_JACK,
                        suite->node(tail).mac);
    suite->node(0).rdc->sync();
    suite->primeTopologyStableAll();

    int champions = 0, supporters = 0;
    for (size_t i = 0; i < suite->nodeCount(); ++i) {
        EXPECT_FALSE(suite->node(i).rdc->isInLoop())
            << "node " << i << " formed a phantom loop over the half-open link";
        if (suite->node(i).chainManager->isChampion()) champions++;
        if (suite->node(i).chainManager->isSupporter()) supporters++;
    }
    EXPECT_EQ(champions, 1)
        << "half-open ring must resolve exactly one champion, not all-supporters";
    EXPECT_EQ(supporters, suite->nodeCount() - 1)
        << "every non-champion backs the one champion";
    EXPECT_TRUE(suite->node(0).chainManager->isChampion())
        << "the open side of the broken link (OUTPUT tail) is the champion";
}

// Self-HELLO echoes must not keep a jack alive: once the real partner stops,
// a jack receiving only its own echoes is still declared dead by the silent
// link (because the self-HELLO is dropped before the liveness stamp).
inline void chainSelfHelloDoesNotRefreshSilentLink(ChainMultiDeviceFixture* suite) {
    suite->spawnDevices(2);
    suite->setAllHunters();
    suite->connectOutputToPrev(1);  // node1.OUTPUT connected to node0.INPUT
    MultiDeviceNode& d1 = suite->node(1);
    d1.rdc->setJackDeadSilentLinkMsForTest(300);
    ASSERT_NE(d1.rdc->getPeerMac(SerialIdentifier::OUTPUT_JACK), nullptr);
    // Partner gone: only self-echoes arrive while time passes past the threshold.
    for (int i = 0; i < 6; ++i) {
        suite->deliverHello(*d1.device, SerialIdentifier::OUTPUT_JACK, d1.mac);
        suite->advanceClock(100);
        d1.rdc->sync();
    }
    EXPECT_EQ(d1.rdc->getPeerMac(SerialIdentifier::OUTPUT_JACK), nullptr)
        << "self-echoes must not keep the silent-link alive";
}

// FDN mid-chain, end to end. node1 simulates an FDN: its game layer never
// installs a role provider, so its BEACON floods role 0 = NONE. NONE must
// sever the duel chain at the FDN: each duelist side is its own head, the
// FDN holds no chain role, and must never read as a same-role duelist the
// walk crosses.
//   d0(B).in ← d1(FDN).out, d1.in ← d2(B).out  (bounty opponent jack = INPUT)
inline void chainFdnMidChainSeversDuelChain(ChainMultiDeviceFixture* suite) {
    suite->spawnDevices(3);
    for (size_t i = 0; i < suite->nodeCount(); ++i) {
        suite->node(i).player->setIsHunter(false);
    }
    // An FDN never runs quickdraw, so nothing ever provides its role byte;
    // mirror that by overriding the spawn wiring with the no-provider default.
    suite->node(1).rdc->setRoleProvider(
        [] { return static_cast<uint8_t>(chain_role::Role::NONE); });

    suite->connectLinearHunterChain();
    suite->deliverAllPackets();
    suite->syncAll();
    suite->deliverAllPackets();
    suite->primeTopologyStableAll();

    MultiDeviceNode& d0 = suite->node(0);
    MultiDeviceNode& d1 = suite->node(1);
    MultiDeviceNode& d2 = suite->node(2);

    // d0's chain ends at the FDN on its opponent jack: d0 is its own champion,
    // NOT a supporter of anything beyond the FDN.
    auto champ0 = d0.chainManager->getChampionMac();
    ASSERT_TRUE(champ0.has_value());
    EXPECT_EQ(memcmp(champ0->data(), d0.mac, 6), 0)
        << "duelist must resolve itself as champion when an FDN sits on its opponent jack";
    EXPECT_TRUE(d0.chainManager->isChampion());

    // The FDN holds no chain role: no champion, neither champion nor supporter.
    EXPECT_FALSE(d1.chainManager->getChampionMac().has_value())
        << "a non-duelist must not resolve a champion";
    EXPECT_FALSE(d1.chainManager->isChampion());
    EXPECT_FALSE(d1.chainManager->isSupporter());

    // The far side resolves independently of the FDN.
    auto champ2 = d2.chainManager->getChampionMac();
    ASSERT_TRUE(champ2.has_value());
    EXPECT_EQ(memcmp(champ2->data(), d2.mac, 6), 0);
}
