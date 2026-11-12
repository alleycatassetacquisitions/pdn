#pragma once

// True multi-device posse test fixture.
//
// Each device owns its own MockDevice, RemoteDeviceCoordinator, Player, and
// PosseManager. Packets emitted via mockPeerComms->sendData(...) are
// captured into a shared queue and routed by MAC to the target device's
// per-type packet handler. Physical connectivity is the production HELLO path:
// each node owns two native serial drivers, a "cable" pumps one node's emitted
// bytes into the other's RX, and the real exec() drain feeds the RDC parser.
//
// Topology convention:
//   Challenger status requires NO same-role peer on the opponent jack, so in an
//   H-H-H line the challenger is the device whose OUTPUT is unconnected. This
//   fixture therefore wires device i's OUTPUT into device (i-1)'s INPUT, which
//   puts the challenger at device 0.

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <array>
#include <memory>
#include <vector>
#include <queue>
#include <cstring>

#include "device-mock.hpp"
#include "rdc-hello-tests.hpp"
#include "utility-tests.hpp"
#include "device/remote-device-coordinator.hpp"
#include "protocol-constants.hpp"
#include "game/posse-manager.hpp"
#include "game/shootout-manager.hpp"
#include "game/player.hpp"
#include "device/drivers/peer-comms-types.hpp"
#include "wireless/mac-functions.hpp"

using ::testing::_;
using ::testing::Invoke;
using ::testing::Return;
using ::testing::WithArgs;

// A single node in the multi-device harness.
struct MultiDeviceNode {
    std::unique_ptr<MockDevice> device;
    // Declared before the RDC so they outlive it: ~RDC clears the byte callbacks
    // it installed on them, as it does on hardware where the drivers are global.
    NativeSerialDriver out{"md-out"};
    NativeSerialDriver in{"md-in"};
    std::unique_ptr<RemoteDeviceCoordinator> rdc;
    std::unique_ptr<Player> player;
    PosseManager* posse = nullptr;
    ShootoutManager* shootout = nullptr;

    /** Frees the owned PosseManager and ShootoutManager. */
    ~MultiDeviceNode() {
        delete shootout;
        delete posse;
    }

    // Per-device captured send-status handlers, one slot per PktType. Packet
    // handlers are not mirrored here: MockPeerComms keeps its own registry, and
    // that one already holds what the WirelessManager installed while the device
    // was being built (the transport's kReliableAck), which no capture wired
    // afterwards can see.
    struct SendStatusSlot {
        PeerCommsInterface::SendStatusCallback handler = nullptr;
        void* ctx = nullptr;
    };
    std::array<SendStatusSlot, static_cast<size_t>(PktType::kNumPacketTypes)> sendStatusSlots{};
    uint8_t mac[6] = {};
};

class PosseMultiDeviceFixture : public ::testing::Test {
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
        cables.clear();
        nodes.clear();
        nodes.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            auto node = std::make_unique<MultiDeviceNode>();
            node->mac[0] = 0x02;
            node->mac[1] = 0x00;
            node->mac[2] = 0x00;
            node->mac[3] = 0x00;
            node->mac[4] = 0x00;
            node->mac[5] = static_cast<uint8_t>(0x10 + i);

            node->device = std::make_unique<MockDevice>();
            node->rdc = std::make_unique<RemoteDeviceCoordinator>();
            node->player = std::make_unique<Player>();

            wirePeerCommsMock(*node);

            node->device->serialManager->setOutputJack(&node->out);
            node->device->serialManager->setInputJack(&node->in);
            // Single-threaded: the fixture emits HELLO itself instead of letting
            // the RDC spawn its cadence task.
            node->rdc->setExternalConnectivityTask(true);
            node->rdc->initialize(node->device->wirelessManager, node->device->serialManager,
                                  node->device.get());

            node->posse =
            new PosseManager(node->player.get(), node->device->wirelessManager, node->rdc.get());
            node->shootout =
            new ShootoutManager(node->player.get(), node->device->wirelessManager, node->rdc.get());
            wireShootoutHandlers(*node);
            Player* p = node->player.get();
            RemoteDeviceCoordinator* r = node->rdc.get();
            PosseManager* c = node->posse;
            r->setSelfProfileProvider([p]() -> PlayerProfile { return p->toProfile(); });
            p->setOnRoleChanged([r, c]() {
                r->resendContext();
                c->onChainStateChanged();
            });

            nodes.push_back(std::move(node));
        }
    }

    // Convenience: set all players to hunters.
    void setAllHunters() {
        for (auto& n : nodes)
            n->player->setIsHunter(true);
    }

    // Wire every device's i+1 OUTPUT to device i INPUT (reverse of task
    // wording for the reason explained at the top of this header) so that
    // in a homogeneous hunter chain, device 0 is the challenger.
    //
    // For i from 1 to N-1:
    //   device i OUTPUT <-> device (i-1) INPUT
    void connectLinearHunterChain() {
        for (size_t i = 1; i < nodes.size(); ++i) {
            connectOutputToPrev(i);
        }
    }

    // Plugs a cable from device i's OUTPUT into device (i-1)'s INPUT, then runs
    // the links until they settle. Both sides end CONNECTED via the real HELLO
    // + ESP-NOW context exchange.
    void connectOutputToPrev(size_t i) {
        ASSERT_LT(i, nodes.size());
        ASSERT_GT(i, 0u);
        cables.push_back({i, i - 1});
        settleLinks();
    }

    /**
     * One HELLO cycle across every plugged cable: emit on both jacks of every
     * node, move the bytes each cable carries, drain them through the real
     * exec() pump, then let each RDC and the radio queue catch up. Unpumped
     * jack output is dropped — an unplugged jack transmits into open air.
     */
    void pumpHelloCycle() {
        for (auto& n : nodes)
            n->rdc->emitHello();
        for (const auto& cable : cables) {
            MultiDeviceNode& upper = *nodes[cable.first];
            MultiDeviceNode& lower = *nodes[cable.second];
            pumpCable(upper.out, lower.in);
            pumpCable(lower.in, upper.out);
        }
        for (auto& n : nodes) {
            n->out.clearOutput();
            n->in.clearOutput();
        }
        for (auto& n : nodes)
            n->rdc->sync(n->device.get());
        deliverAllPackets();
    }

    /**
     * Enough HELLO cycles for a fresh cable to reach CONNECTED and for the head
     * MAC (and any ring closure it implies) to propagate the length of the chain.
     */
    void settleLinks() {
        for (size_t round = 0; round < nodes.size() + 8; ++round) {
            pumpHelloCycle();
            fakeClock->advance(RemoteDeviceCoordinator::HELLO_CADENCE_MS);
        }
    }

    /**
     * Ring closure the way production delivers it: the head's RDC callback, the
     * roster broadcast, then each member's Idle -> ShootoutProposal mount.
     *
     * Index of the node that latched the ring, set by claimRing().
     */
    size_t ringHeadIndex = 0;

    /**
     * Claims on whichever node latched, which `closeRing()` makes the last-indexed
     * one. Production fires onRingClosed() only from that RDC callback, so a
     * self-claim on any other node is a state no device can reach. The roster is
     * injected because the fixture does not route kConnectionAnnounce (see
     * isRouted), so no roster ever reaches a head here.
     */
    void claimRing() {
        size_t headIndex = nodes.size();
        for (size_t i = 0; i < nodes.size(); ++i) {
            if (nodes[i]->rdc->getChainRole() == ChainRole::RING) {
                headIndex = i;
                break;
            }
        }
        ASSERT_LT(headIndex, nodes.size()) << "closeRing() left no node latched";
        ringHeadIndex = headIndex;
        std::vector<std::array<uint8_t, 6>> roster;
        for (auto& n : nodes) {
            std::array<uint8_t, 6> mac;
            memcpy(mac.data(), n->mac, 6);
            roster.push_back(mac);
        }
        nodes[headIndex]->shootout->setRingMembersForTest(roster);
        nodes[headIndex]->shootout->onRingClosed();
        deliverAllPackets();
        for (auto& n : nodes) {
            if (n->shootout->shouldEnterProposal()) n->shootout->startProposal();
        }
    }

    /** Advances the clock on all devices in lockstep. */
    void advanceClock(unsigned long ms) { fakeClock->advance(ms); }

    void syncAll() {
        pumpHelloCycle();
        for (auto& n : nodes) {
            n->device->wirelessManager->sync();
            n->posse->sync();
            n->shootout->sync();
        }
    }

    /** Pumps one HELLO cycle, dropping one conductor on cable (upperIdx, lowerIdx). */
    void pumpHelloCycleDropping(size_t upperIdx, size_t lowerIdx, bool dropUpperToLower = false) {
        for (auto& n : nodes)
            n->rdc->emitHello();
        for (const auto& cable : cables) {
            MultiDeviceNode& upper = *nodes[cable.first];
            MultiDeviceNode& lower = *nodes[cable.second];
            const bool targetCable = (cable.first == upperIdx && cable.second == lowerIdx);
            if (!targetCable || !dropUpperToLower) {
                pumpCable(upper.out, lower.in);
            }
            if (!targetCable || dropUpperToLower) {
                pumpCable(lower.in, upper.out);
            }
        }
        for (auto& n : nodes) {
            n->out.clearOutput();
            n->in.clearOutput();
        }
        for (auto& n : nodes)
            n->rdc->sync(n->device.get());
        deliverAllPackets();
        for (auto& n : nodes) {
            n->device->wirelessManager->sync();
            n->posse->sync();
            n->shootout->sync();
        }
    }

    // Close a linear chain into a ring with the last cable: device 0's OUTPUT
    // into the tail's INPUT.
    void closeRing() {
        ASSERT_GE(nodes.size(), 2u);
        cables.push_back({0, nodes.size() - 1});
        settleLinks();
        syncAll();
        deliverAllPackets();
        seedRingRoster();
    }

    /**
     * Seeds every node's ring roster directly because dispatch() drops
     * kConnectionAnnounce before it reaches the head.
     */
    void seedRingRoster() {
        std::vector<std::array<uint8_t, 6>> members;
        for (auto& n : nodes) {
            std::array<uint8_t, 6> mac;
            memcpy(mac.data(), n->mac, 6);
            members.push_back(mac);
        }
        for (auto& n : nodes)
            n->shootout->setRingMembersForTest(members);
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
    // Plugged cables as (upper, lower): upper's OUTPUT into lower's INPUT.
    std::vector<std::pair<size_t, size_t>> cables;
    std::queue<PendingPacket> pending;
    FakePlatformClock* fakeClock = nullptr;

    size_t indexOfMac(const uint8_t* mac) const {
        for (size_t i = 0; i < nodes.size(); ++i) {
            if (memcmp(nodes[i]->mac, mac, 6) == 0) return i;
        }
        return SIZE_MAX;
    }

    // Register Shootout command + ack handlers. Mirrors the dispatcher in
    // the GameSession constructor so every packet path exercised on hardware is
    // exercised in the fixture too.
    void wireShootoutHandlers(MultiDeviceNode& n) {
        ShootoutManager* mgr = n.shootout;

        n.device->wirelessManager->setEspNowPacketHandler(
        PktType::kShootoutCommand,
        [](const uint8_t* fromMac, const uint8_t* data, const size_t dataLen, void* ctx) {
            auto* m = static_cast<ShootoutManager*>(ctx);
            if (dataLen < 2) return;
            ShootoutCmd cmd = static_cast<ShootoutCmd>(data[0]);
            uint8_t seqId = data[1];
            const uint8_t* payload = data + 2;
            size_t payloadLen = dataLen - 2;
            switch (cmd) {
                case ShootoutCmd::BRACKET: {
                    if (payloadLen < 1) break;
                    uint8_t count = payload[0];
                    if (payloadLen < 1 + 6u * static_cast<size_t>(count)) break;
                    std::vector<std::array<uint8_t, 6>> macs;
                    for (uint8_t i = 0; i < count; i++) {
                        std::array<uint8_t, 6> mac;
                        memcpy(mac.data(), payload + 1 + 6 * i, 6);
                        macs.push_back(mac);
                    }
                    m->onBracketReceived(fromMac, macs, seqId);
                    break;
                }
                case ShootoutCmd::MATCH_START:
                    if (payloadLen >= 13)
                        m->onMatchStartReceived(fromMac, payload, payload + 6, payload[12], seqId);
                    break;
                case ShootoutCmd::MATCH_RESULT:
                    if (payloadLen >= 13)
                        m->onMatchResultReceived(payload, payload + 6, payload[12], seqId, fromMac);
                    break;
                case ShootoutCmd::TOURNAMENT_END:
                    if (payloadLen >= 6) m->onTournamentEndReceived(fromMac, payload, seqId);
                    break;
                case ShootoutCmd::ABORT: m->onAbortReceived(fromMac, seqId); break;
            }
        },
        mgr);

        n.device->wirelessManager->setEspNowPacketHandler(
        PktType::kShootoutCommandAck,
        [](const uint8_t* fromMac, const uint8_t* data, const size_t dataLen, void* ctx) {
            // Same routing as GameSession::onShootoutCommandAckPacket: the
            // seqId alone names the frame, so the command byte is only
            // range-checked, never dispatched on.
            auto* m = static_cast<ShootoutManager*>(ctx);
            if (dataLen < 2) return;
            if (data[0] > static_cast<uint8_t>(ShootoutCmd::ABORT)) return;
            m->onCommandAckReceived(fromMac, data[1]);
        },
        mgr);
    }

    /**
     * The PktTypes this fixture carries between nodes. Deliberately not every
     * type: kConnectionAnnounce is dropped so no roster ever reaches a head here
     * (claimRing and seedRingRoster inject rosters instead), and the cases below
     * were written against that.
     */
    static bool isRouted(PktType type) {
        switch (type) {
            case PktType::kPdnConnectionContext:
            case PktType::kRoleAnnounce:
            case PktType::kChainConfirm:
            case PktType::kChainJoin:
            case PktType::kChainGameEvent:
            case PktType::kReliableAck:
            case PktType::kShootoutRingClosed:
            case PktType::kShootoutConfirm:
            case PktType::kShootoutCommand:
            case PktType::kShootoutCommandAck: return true;
            default: return false;
        }
    }

    void dispatch(const PendingPacket& p) {
        // A broadcast frame lands on every node in range except its sender,
        // which is what the radio does with the permanent broadcast peer.
        if (memcmp(p.toMac.data(), MockDevice::BROADCAST_MAC, 6) == 0) {
            for (size_t i = 0; i < nodes.size(); ++i) {
                if (i == p.fromIndex) continue;
                PendingPacket unicast = p;
                memcpy(unicast.toMac.data(), nodes[i]->mac, 6);
                dispatch(unicast);
            }
            return;
        }
        size_t toIdx = indexOfMac(p.toMac.data());
        if (toIdx == SIZE_MAX) return;  // MAC unknown — drop (mirrors real ESP-NOW gating).
        if (!isRouted(p.type)) return;

        MultiDeviceNode& target = *nodes[toIdx];
        MultiDeviceNode& source = *nodes[p.fromIndex];

        if (!target.device->mockPeerComms->triggerPacketCallback(source.mac, p.type, p.data.data(),
                                                                 p.data.size())) {
            return;
        }

        // Report delivery back to the sender: on channels with no reply packet
        // that report IS the delivery signal, and a fixture that routed the frame
        // without it would make every send look undelivered and run out its span.
        //
        // Friendlier than the real radio, deliberately. This sits after the
        // no-handler return, so delivery is reported only when the receiver had a
        // handler and it ran — whereas a real SEND_SUCCESS is a MAC-layer verdict
        // that says nothing about the application. The case the production code
        // worries about, the radio acking while the peer is deaf, therefore
        // cannot be reproduced here; it needs a test that drives onSendResult by
        // hand. Only the two types the cases were written against get it.
        if (p.type != PktType::kPdnConnectionContext && p.type != PktType::kRoleAnnounce) return;
        const MultiDeviceNode::SendStatusSlot& status =
        source.sendStatusSlots[static_cast<size_t>(p.type)];
        if (status.handler) {
            status.handler(p.toMac.data(), p.data.data(), p.data.size(), /*success=*/true,
                           status.ctx);
        }
    }

    void wirePeerCommsMock(MultiDeviceNode& n) {
        MockPeerComms* pc = n.device->mockPeerComms;

        ON_CALL(*pc, getMacAddress()).WillByDefault(Return(n.mac));
        ON_CALL(*pc, getPeerCommsState()).WillByDefault(Return(PeerCommsState::ENABLED));

        // Capture every outgoing packet into the shared queue.
        size_t selfIdx = nodes.size();  // this node's index once push_back completes
        ON_CALL(*pc, sendData(_, _, _, _))
        .WillByDefault([this, selfIdx](const uint8_t* dst, PktType type, const uint8_t* data,
                                       const size_t len) -> int {
            PendingPacket p;
            p.fromIndex = selfIdx;
            memcpy(p.toMac.data(), dst, 6);
            p.type = type;
            p.data.assign(data, data + len);
            pending.push(std::move(p));
            return 1;
        });

        ON_CALL(*pc, setSendStatusHandler(_, _, _))
        .WillByDefault([&n](PktType type, PeerCommsInterface::SendStatusCallback cb, void* ctx) {
            n.sendStatusSlots[static_cast<size_t>(type)] = {cb, ctx};
        });
        ON_CALL(*pc, clearSendStatusHandler(_)).WillByDefault([&n](PktType type) {
            n.sendStatusSlots[static_cast<size_t>(type)] = {};
        });
    }
};

// ============================================================================
// Tests exercising the multi-device fixture.
// ============================================================================

// H-H-H linear chain: device 0 is challenger (OUTPUT tail), devices 1 and 2 are
// supporters whose challengerMac caches device 0's MAC after the role-announce
// cascade propagates.
inline void posseMultiDeviceChainFormsAndElectsChallenger(PosseMultiDeviceFixture* suite) {
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

    // Role election: device 0 has no OUTPUT-jack peer so isChallenger() should be
    // true once it observes its supporter-jack peer. Drive onChainStateChanged
    // explicitly to match what GameSession does.
    d0.posse->onChainStateChanged();
    d1.posse->onChainStateChanged();
    d2.posse->onChainStateChanged();
    suite->deliverAllPackets();
    // A second pass lets the cascaded role-announce from d1 reach d2.
    d0.posse->onChainStateChanged();
    d1.posse->onChainStateChanged();
    d2.posse->onChainStateChanged();
    suite->deliverAllPackets();

    EXPECT_TRUE(d0.posse->isChallenger());
    EXPECT_FALSE(d1.posse->isChallenger());
    EXPECT_FALSE(d2.posse->isChallenger());

    EXPECT_TRUE(d1.posse->isSupporter());
    EXPECT_TRUE(d2.posse->isSupporter());

    // Both supporters should have learned d0's MAC as challengerMac.
    ASSERT_NE(d1.posse->getChallengerMac(), nullptr);
    EXPECT_EQ(memcmp(d1.posse->getChallengerMac(), d0.mac, 6), 0);
    ASSERT_NE(d2.posse->getChallengerMac(), nullptr);
    EXPECT_EQ(memcmp(d2.posse->getChallengerMac(), d0.mac, 6), 0);
}

// H-H-H linear chain: the challenger counts the supporter two cables away as
// readily as the one on its own cable. The far one is placed by the join it
// sent when the role cascade named its challenger, nothing else can see it.
inline void posseMultiDeviceConfirmDeliveredToChallenger(PosseMultiDeviceFixture* suite) {
    suite->spawnDevices(3);
    suite->setAllHunters();
    suite->connectLinearHunterChain();
    suite->deliverAllPackets();
    suite->syncAll();
    suite->deliverAllPackets();

    MultiDeviceNode& d0 = suite->node(0);
    MultiDeviceNode& d1 = suite->node(1);
    MultiDeviceNode& d2 = suite->node(2);

    d0.posse->onChainStateChanged();
    d1.posse->onChainStateChanged();
    d2.posse->onChainStateChanged();
    suite->deliverAllPackets();
    d0.posse->onChainStateChanged();
    d1.posse->onChainStateChanged();
    d2.posse->onChainStateChanged();
    suite->deliverAllPackets();

    ASSERT_TRUE(d0.posse->isChallenger());
    ASSERT_NE(d2.posse->getChallengerMac(), nullptr);
    ASSERT_EQ(memcmp(d2.posse->getChallengerMac(), d0.mac, 6), 0);
    ASSERT_EQ(d0.posse->getBoostMs(), 0u);

    // D2 is two cables from the challenger and shares none of them with it.
    d2.posse->sendConfirm();
    suite->deliverAllPackets();

    EXPECT_EQ(d0.posse->getConfirmedSupporterCount(), 1u);
    EXPECT_EQ(d0.posse->getBoostMs(), PosseManager::BOOST_PER_SUPPORTER_MS);

    // D1 is on the challenger's own cable, so its confirm counts too.
    d1.posse->sendConfirm();
    suite->deliverAllPackets();

    EXPECT_EQ(d0.posse->getConfirmedSupporterCount(), 2u);
    EXPECT_EQ(d0.posse->getBoostMs(), 2 * PosseManager::BOOST_PER_SUPPORTER_MS);
}

// Boost is the reason the chain exists: it has to grow with the chain, not sit
// at one supporter's worth however many devices are plugged in behind it. Four
// hunters, three supporters at one, two and three cables of distance, all three
// counted and all three paid.
inline void posseMultiDeviceBoostScalesWithChainDepth(PosseMultiDeviceFixture* suite) {
    suite->spawnDevices(4);
    suite->setAllHunters();
    suite->connectLinearHunterChain();
    suite->deliverAllPackets();
    suite->syncAll();
    suite->deliverAllPackets();

    // Two passes: the cascade advances one cable per pass, so the tail needs the
    // second to learn the challenger and announce itself back.
    for (int pass = 0; pass < 3; ++pass) {
        for (size_t i = 0; i < suite->nodeCount(); ++i) {
            suite->node(i).posse->onChainStateChanged();
        }
        suite->deliverAllPackets();
    }

    MultiDeviceNode& challenger = suite->node(0);
    ASSERT_TRUE(challenger.posse->isChallenger());
    for (size_t i = 1; i < suite->nodeCount(); ++i) {
        ASSERT_NE(suite->node(i).posse->getChallengerMac(), nullptr) << "node " << i;
        ASSERT_EQ(memcmp(suite->node(i).posse->getChallengerMac(), challenger.mac, 6), 0)
        << "node " << i << " follows the wrong challenger";
    }
    // Every supporter is on the roster before a single press, which is what lets
    // a COUNTDOWN reach a device that has not confirmed yet.
    ASSERT_EQ(challenger.posse->getSupporterChainPeers().size(), 3u);

    for (size_t i = 1; i < suite->nodeCount(); ++i) {
        suite->node(i).posse->sendConfirm();
        suite->deliverAllPackets();
    }

    EXPECT_EQ(challenger.posse->getConfirmedSupporterCount(), 3u);
    EXPECT_EQ(challenger.posse->getBoostMs(), 3 * PosseManager::BOOST_PER_SUPPORTER_MS);
}

// The challenger's COUNTDOWN/WIN/LOSS have to land on every supporter, not just
// the one it shares a cable with — a supporter three cables away never sees a
// round start otherwise, and never leaves its stale screen.
inline void posseMultiDeviceGameEventReachesDistantSupporter(PosseMultiDeviceFixture* suite) {
    suite->spawnDevices(4);
    suite->setAllHunters();
    suite->connectLinearHunterChain();
    suite->deliverAllPackets();
    suite->syncAll();
    suite->deliverAllPackets();

    for (int pass = 0; pass < 3; ++pass) {
        for (size_t i = 0; i < suite->nodeCount(); ++i) {
            suite->node(i).posse->onChainStateChanged();
        }
        suite->deliverAllPackets();
    }

    MultiDeviceNode& challenger = suite->node(0);
    ASSERT_TRUE(challenger.posse->isChallenger());

    // Counted after admission, so a node only scores an event its posse took.
    std::vector<int> accepted(suite->nodeCount(), 0);
    for (size_t i = 0; i < suite->nodeCount(); ++i) {
        int* counter = &accepted[i];
        suite->node(i).posse->setOnGameEvent([counter](uint8_t) { (*counter)++; });
    }

    challenger.posse->sendGameEventToSupporters(ChainGameEventType::COUNTDOWN);
    suite->deliverAllPackets();

    EXPECT_EQ(accepted[0], 0) << "challenger should not receive its own broadcast";
    for (size_t i = 1; i < suite->nodeCount(); ++i) {
        EXPECT_EQ(accepted[i], 1) << "node " << i << " missed the COUNTDOWN";
    }

    // Every supporter, at every depth, filters on the challenger the event names.
    for (size_t i = 1; i < suite->nodeCount(); ++i) {
        EXPECT_TRUE(suite->node(i).posse->isEventFromOwnChallenger(challenger.mac))
        << "node " << i << " would reject its own challenger's event";
    }
    uint8_t strangerChallenger[6] = {0x77, 0x77, 0x77, 0x77, 0x77, 0x77};
    EXPECT_FALSE(suite->node(3).posse->isEventFromOwnChallenger(strangerChallenger));
}

// End-to-end Shootout consensus: 4 devices form a ring, each confirms,
// every device reaches BRACKET_REVEAL phase, coordinator broadcasts
// MATCH_START and both players transition into MATCH_IN_PROGRESS.
inline void shootoutFourDeviceConsensusAndMatchStart(PosseMultiDeviceFixture* suite) {
    suite->spawnDevices(4);
    suite->setAllHunters();
    suite->connectLinearHunterChain();
    suite->deliverAllPackets();
    suite->syncAll();
    suite->deliverAllPackets();
    suite->closeRing();

    // Every device should now see the ring as closed.
    for (size_t i = 0; i < suite->nodeCount(); ++i) {
        ASSERT_TRUE(suite->node(i).rdc->isInRing()) << "node " << i << " does not see the ring";
    }

    suite->claimRing();
    for (size_t i = 0; i < suite->nodeCount(); ++i) {
        EXPECT_EQ(suite->node(i).shootout->getPhase(), ShootoutManager::Phase::PROPOSAL)
        << "node " << i << " missed the ring-closed broadcast";
    }

    // Each device confirms. After four confirms propagate, every device
    // should reach BRACKET_REVEAL; the coordinator generates+broadcasts
    // the bracket.
    for (size_t i = 0; i < suite->nodeCount(); ++i) {
        suite->node(i).shootout->confirmLocal();
        suite->deliverAllPackets();
    }
    suite->deliverAllPackets();
    suite->syncAll();
    suite->deliverAllPackets();

    for (size_t i = 0; i < suite->nodeCount(); ++i) {
        EXPECT_EQ(suite->node(i).shootout->getPhase(), ShootoutManager::Phase::BRACKET_REVEAL)
        << "node " << i << " phase";
    }
    EXPECT_TRUE(suite->node(suite->ringHeadIndex).shootout->isCoordinator());

    // Advance past the bracket-reveal window and let the coordinator fire
    // the first MATCH_START.
    suite->advanceClock(ShootoutManager::kBracketRevealMs + 100);
    suite->syncAll();
    suite->deliverAllPackets();
    suite->syncAll();
    suite->deliverAllPackets();

    // Exactly one coordinator. Coordinator should be MATCH_IN_PROGRESS.
    // Two players should also be MATCH_IN_PROGRESS; spectators too
    // (phase is per-device, and receiving MATCH_START sets phase on
    // everyone).
    int inProgress = 0;
    int activePlayers = 0;
    for (size_t i = 0; i < suite->nodeCount(); ++i) {
        if (suite->node(i).shootout->getPhase() == ShootoutManager::Phase::MATCH_IN_PROGRESS) {
            inProgress++;
        }
        if (suite->node(i).shootout->isInCurrentMatch()) activePlayers++;
    }
    EXPECT_EQ(inProgress, static_cast<int>(suite->nodeCount()));
    EXPECT_EQ(activePlayers, 2);
}

// Drive a 4-device tournament from ring closure through TOURNAMENT_END. Covers
// advance-round logic: expected match count is 3 (2 round-1 matches, 1 final).
// Guards against regression where round advances after only 1 match or stalls
// between matches.
inline void shootoutFourDeviceFullTournament(PosseMultiDeviceFixture* suite) {
    suite->spawnDevices(4);
    suite->setAllHunters();
    suite->connectLinearHunterChain();
    suite->deliverAllPackets();
    suite->syncAll();
    suite->deliverAllPackets();
    suite->closeRing();

    suite->claimRing();
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

    auto activePlayerIndex = [&]() -> int {
        for (size_t i = 0; i < suite->nodeCount(); ++i) {
            if (suite->node(i).shootout->isInCurrentMatch()) return (int)i;
        }
        return -1;
    };

    // Drive up to 4 matches (safety cap). Real count should be 3.
    int matchesDriven = 0;
    for (int m = 0; m < 6; ++m) {
        int playerIdx = activePlayerIndex();
        if (playerIdx < 0) break;
        // Have this player report a win. The other flow (opponent wins) is
        // symmetric; picking one side keeps the test deterministic.
        suite->node(playerIdx).shootout->reportLocalWin();
        suite->deliverAllPackets();
        suite->syncAll();
        suite->deliverAllPackets();
        suite->syncAll();
        suite->deliverAllPackets();
        matchesDriven++;

        // Check tournament end.
        bool anyEnded = false;
        for (size_t i = 0; i < suite->nodeCount(); ++i) {
            if (suite->node(i).shootout->getPhase() == ShootoutManager::Phase::ENDED) {
                anyEnded = true;
                break;
            }
        }
        if (anyEnded) break;
    }

    EXPECT_EQ(matchesDriven, 3) << "4-device SE bracket should run exactly 3 matches";
    // All nodes end in ENDED phase with the same winner.
    std::array<uint8_t, 6> winner{};
    bool winnerSet = false;
    for (size_t i = 0; i < suite->nodeCount(); ++i) {
        auto phase = suite->node(i).shootout->getPhase();
        EXPECT_EQ(phase, ShootoutManager::Phase::ENDED) << "node " << i << " phase not ENDED";
        auto wArr = suite->node(i).shootout->getTournamentWinner();
        if (!winnerSet) {
            winner = wArr;
            winnerSet = true;
        } else {
            EXPECT_EQ(memcmp(winner.data(), wArr.data(), 6), 0)
            << "node " << i << " disagrees on winner";
        }
    }
}

// 8-device tournament: 4+2+1 = 7 matches across 3 rounds. Catches off-by-one
// errors in advance-round that only show up beyond the first round. Also
// validates player-name propagation: every node sets a distinct name, and
// every other node can resolve that name from its ShootoutManager after
// CONFIRMs propagate.
inline void shootoutEightDeviceFullTournament(PosseMultiDeviceFixture* suite) {
    suite->spawnDevices(8);
    suite->setAllHunters();
    suite->connectLinearHunterChain();
    suite->deliverAllPackets();
    suite->syncAll();
    suite->deliverAllPackets();
    suite->closeRing();

    static const char* kNames[8] = {"alice", "bob",   "carol", "dave",
                                    "erin",  "frank", "gina",  "hank"};
    for (size_t i = 0; i < suite->nodeCount(); ++i) {
        suite->node(i).player->setName(kNames[i]);
    }

    suite->claimRing();
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
    for (int m = 0; m < 12; ++m) {
        int playerIdx = -1;
        for (size_t i = 0; i < suite->nodeCount(); ++i) {
            if (suite->node(i).shootout->isInCurrentMatch()) {
                playerIdx = (int)i;
                break;
            }
        }
        if (playerIdx < 0) break;
        suite->node(playerIdx).shootout->reportLocalWin();
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
    EXPECT_EQ(matches, 7) << "8-device SE bracket should run exactly 7 matches";
    for (size_t i = 0; i < suite->nodeCount(); ++i) {
        EXPECT_EQ(suite->node(i).shootout->getPhase(), ShootoutManager::Phase::ENDED)
        << "8-device node " << i;
    }

    // Every node should know every other node's name after CONFIRM propagation.
    for (size_t observer = 0; observer < suite->nodeCount(); ++observer) {
        for (size_t target = 0; target < suite->nodeCount(); ++target) {
            std::string resolved =
            suite->node(observer).shootout->getNameForMac(suite->node(target).mac);
            EXPECT_EQ(resolved, kNames[target])
            << "node " << observer << " can't resolve node " << target;
        }
    }
}

// Two tournaments on one ring, with a reset between them. The ring has to
// survive the whole first tournament, including its bracket-reveal wait, or the
// second one has no members to bracket.
inline void shootoutFourDeviceTwoTournamentsBackToBack(PosseMultiDeviceFixture* suite) {
    suite->spawnDevices(4);
    suite->setAllHunters();
    suite->connectLinearHunterChain();
    suite->deliverAllPackets();
    suite->syncAll();
    suite->deliverAllPackets();
    suite->closeRing();

    auto runOne = [&]() {
        suite->claimRing();
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
            int playerIdx = -1;
            for (size_t i = 0; i < suite->nodeCount(); ++i) {
                if (suite->node(i).shootout->isInCurrentMatch()) {
                    playerIdx = (int)i;
                    break;
                }
            }
            if (playerIdx < 0) break;
            suite->node(playerIdx).shootout->reportLocalWin();
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

    // The ring must still be intact for tournament 2.
    for (size_t i = 0; i < suite->nodeCount(); ++i) {
        ASSERT_TRUE(suite->node(i).rdc->isInRing()) << "post-reset ring broken on node " << i;
    }

    // Run tournament 2. runOne re-claims the ring, which is what re-seeds the
    // ring-member set that resetToIdle just dropped.
    int matchesTwo = runOne();
    for (size_t i = 0; i < suite->nodeCount(); ++i) {
        ASSERT_EQ(suite->node(i).shootout->getRingMembers().size(), 4u)
        << "ring members wrong on node " << i;
    }
    EXPECT_EQ(matchesTwo, 3) << "tournament 2 did not run 3 matches";
    for (size_t i = 0; i < suite->nodeCount(); ++i) {
        EXPECT_EQ(suite->node(i).shootout->getPhase(), ShootoutManager::Phase::ENDED)
        << "tourn 2 node " << i;
    }
}

inline void posseMultiDeviceOneSidedHelloDropoutRecoversChain(PosseMultiDeviceFixture* suite) {
    suite->spawnDevices(3);
    suite->setAllHunters();
    suite->connectLinearHunterChain();
    suite->deliverAllPackets();
    suite->syncAll();
    suite->deliverAllPackets();

    MultiDeviceNode& d0 = suite->node(0);
    MultiDeviceNode& d1 = suite->node(1);
    MultiDeviceNode& d2 = suite->node(2);

    for (int pass = 0; pass < 2; ++pass) {
        d0.posse->onChainStateChanged();
        d1.posse->onChainStateChanged();
        d2.posse->onChainStateChanged();
        suite->deliverAllPackets();
    }

    ASSERT_TRUE(d0.posse->isChallenger());
    ASSERT_TRUE(d1.posse->isSupporter());
    ASSERT_TRUE(d2.posse->isSupporter());
    ASSERT_NE(d1.posse->getChallengerMac(), nullptr);
    ASSERT_EQ(memcmp(d1.posse->getChallengerMac(), d0.mac, 6), 0);
    ASSERT_NE(d2.posse->getChallengerMac(), nullptr);
    ASSERT_EQ(memcmp(d2.posse->getChallengerMac(), d0.mac, 6), 0);

    for (int tick = 0; tick < 6; ++tick) {
        suite->advanceClock(RemoteDeviceCoordinator::HELLO_CADENCE_MS);
        suite->pumpHelloCycleDropping(/*upperIdx=*/1, /*lowerIdx=*/0);
    }

    ASSERT_EQ(d0.rdc->getJackStatus(SerialIdentifier::INPUT_JACK), JackStatus::CONNECTED);
    ASSERT_EQ(d1.rdc->getJackStatus(SerialIdentifier::OUTPUT_JACK), JackStatus::DISCONNECTED);
    ASSERT_TRUE(d1.posse->isChallenger());

    suite->settleLinks();
    suite->syncAll();
    suite->deliverAllPackets();

    EXPECT_TRUE(d0.posse->isChallenger());
    EXPECT_TRUE(d1.posse->isSupporter());
    EXPECT_TRUE(d2.posse->isSupporter());
    ASSERT_NE(d1.posse->getChallengerMac(), nullptr);
    EXPECT_EQ(memcmp(d1.posse->getChallengerMac(), d0.mac, 6), 0);
    ASSERT_NE(d2.posse->getChallengerMac(), nullptr);
    EXPECT_EQ(memcmp(d2.posse->getChallengerMac(), d0.mac, 6), 0);

    d1.posse->sendConfirm();
    d2.posse->sendConfirm();
    suite->deliverAllPackets();
    EXPECT_EQ(d0.posse->getConfirmedSupporterCount(), 2u);
    EXPECT_EQ(d0.posse->getBoostMs(), 2 * PosseManager::BOOST_PER_SUPPORTER_MS);

    suite->advanceClock(RemoteDeviceCoordinator::CONTEXT_EXCHANGE_TIMEOUT_MS + 1);
    for (int tick = 0; tick < 6; ++tick) {
        suite->advanceClock(RemoteDeviceCoordinator::HELLO_CADENCE_MS);
        suite->pumpHelloCycleDropping(/*upperIdx=*/1, /*lowerIdx=*/0, /*dropUpperToLower=*/true);
    }
    ASSERT_EQ(d0.rdc->getJackStatus(SerialIdentifier::INPUT_JACK), JackStatus::DISCONNECTED);
    ASSERT_EQ(d1.rdc->getJackStatus(SerialIdentifier::OUTPUT_JACK), JackStatus::CONNECTED);
    ASSERT_EQ(d0.posse->getConfirmedSupporterCount(), 0u);

    suite->settleLinks();
    suite->syncAll();
    suite->deliverAllPackets();

    EXPECT_EQ(d0.posse->getSupporterChainPeers().size(), 2u);
    EXPECT_EQ(d0.posse->getConfirmedSupporterCount(), 2u);
    EXPECT_EQ(d0.posse->getBoostMs(), 2 * PosseManager::BOOST_PER_SUPPORTER_MS);
}

inline void
posseMultiDeviceLiveRoleFlipAndOneSidedDropoutRecoverMatchGate(PosseMultiDeviceFixture* suite) {
    suite->spawnDevices(2);
    suite->setAllHunters();
    suite->connectLinearHunterChain();
    suite->deliverAllPackets();
    suite->syncAll();
    suite->deliverAllPackets();

    MultiDeviceNode& d0 = suite->node(0);
    MultiDeviceNode& d1 = suite->node(1);

    EXPECT_FALSE(d1.posse->canInitiateMatch());

    d0.player->setIsHunter(false);
    suite->deliverAllPackets();
    EXPECT_TRUE(d1.posse->canInitiateMatch());

    suite->advanceClock(RemoteDeviceCoordinator::CONTEXT_EXCHANGE_TIMEOUT_MS + 1);
    for (int tick = 0; tick < 6; ++tick) {
        suite->advanceClock(RemoteDeviceCoordinator::HELLO_CADENCE_MS);
        suite->pumpHelloCycleDropping(/*upperIdx=*/1, /*lowerIdx=*/0);
    }
    ASSERT_EQ(d0.rdc->getJackStatus(SerialIdentifier::INPUT_JACK), JackStatus::CONNECTED);
    ASSERT_EQ(d1.rdc->getJackStatus(SerialIdentifier::OUTPUT_JACK), JackStatus::DISCONNECTED);
    EXPECT_FALSE(d1.posse->canInitiateMatch());

    suite->settleLinks();
    suite->syncAll();
    suite->deliverAllPackets();
    EXPECT_TRUE(d1.posse->canInitiateMatch());
}
