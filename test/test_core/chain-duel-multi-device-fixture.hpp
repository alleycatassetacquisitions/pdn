#pragma once

// True multi-device chain duel test fixture.
//
// Each device owns its own MockDevice, RemoteDeviceCoordinator, Player, and
// ChainDuelManager. Packets emitted via mockPeerComms->sendData(...) are
// captured into a shared queue and routed by MAC to the target device's
// per-type packet handler. Physical connectivity is the production HELLO path:
// each node owns two native serial drivers, a "cable" pumps one node's emitted
// bytes into the other's RX, and the real exec() drain feeds the RDC parser.
//
// Topology convention:
//   Champion status requires NO same-role peer on the opponent jack, so in an
//   H-H-H line the champion is the device whose OUTPUT is unconnected. This
//   fixture therefore wires device i's OUTPUT into device (i-1)'s INPUT, which
//   puts the champion at device 0.

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <memory>
#include <vector>
#include <queue>
#include <cstring>

#include "device-mock.hpp"
#include "rdc-hello-tests.hpp"
#include "utility-tests.hpp"
#include "device/remote-device-coordinator.hpp"
#include "protocol-constants.hpp"
#include "game/chain-duel-manager.hpp"
#include "game/shootout-manager.hpp"
#include "game/player.hpp"
#include "device/drivers/peer-comms-types.hpp"
#include "wireless/mac-functions.hpp"

using ::testing::_;
using ::testing::Return;
using ::testing::Invoke;
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
    std::unique_ptr<ChainDuelManager> cdm;
    std::unique_ptr<ShootoutManager> shootout;

    // Per-device captured handlers (one slot per PktType the fixture routes).
    PeerCommsInterface::PacketCallback contextHandler = nullptr;
    void* contextCtx = nullptr;
    PeerCommsInterface::PacketCallback roleAnnounceHandler = nullptr;
    void* roleAnnounceCtx = nullptr;
    PeerCommsInterface::PacketCallback roleAnnounceAckHandler = nullptr;
    void* roleAnnounceAckCtx = nullptr;
    PeerCommsInterface::PacketCallback chainConfirmHandler = nullptr;
    void* chainConfirmCtx = nullptr;
    PeerCommsInterface::PacketCallback chainJoinHandler = nullptr;
    void* chainJoinCtx = nullptr;
    PeerCommsInterface::PacketCallback chainGameEventHandler = nullptr;
    void* chainGameEventCtx = nullptr;
    PeerCommsInterface::PacketCallback shootoutHandler = nullptr;
    void* shootoutCtx = nullptr;
    PeerCommsInterface::PacketCallback shootoutAckHandler = nullptr;
    void* shootoutAckCtx = nullptr;

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
        cables.clear();
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

            node->device->serialManager->setOutputJack(&node->out);
            node->device->serialManager->setInputJack(&node->in);
            // Single-threaded: the fixture emits HELLO itself instead of letting
            // the RDC spawn its cadence task.
            node->rdc->setExternalConnectivityTask(true);
            node->rdc->initialize(node->device->wirelessManager,
                                  node->device->serialManager,
                                  node->device.get());

            // CDM and chain-event handlers register after RDC so they don't
            // compete with RDC's own setPacketHandler calls.
            node->cdm = std::make_unique<ChainDuelManager>(node->player.get(),
                                                          node->device->wirelessManager,
                                                          node->rdc.get());
            node->shootout = std::make_unique<ShootoutManager>(node->player.get(),
                                                               node->device->wirelessManager,
                                                               node->rdc.get());
            wireChainEventHandlers(*node);
            wireShootoutHandlers(*node);

            // Hook CDM to RDC chain-change notifications (what GameSession does).
            ChainDuelManager* cdmRaw = node->cdm.get();
            node->rdc->setChainChangeCallback([cdmRaw]() {
                cdmRaw->onChainStateChanged();
            });
            // peerLostCallback intentionally unwired — advanceClock() expires
            // HELLO liveness and would fire it spuriously. Direct-path coverage
            // lives in RDCHelloTests + ShootoutManagerTests.

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

    // Plugs a cable from device i's OUTPUT into device (i-1)'s INPUT, then runs
    // the links until they settle. Both sides end CONNECTED via the real HELLO
    // + ESP-NOW context exchange.
    void connectOutputToPrev(size_t i) {
        ASSERT_LT(i, nodes.size());
        ASSERT_GT(i, 0u);
        cables.push_back({i, i - 1});
        settleLinks();
    }

    /// One HELLO cycle across every plugged cable: emit on both jacks of every
    /// node, move the bytes each cable carries, drain them through the real
    /// exec() pump, then let each RDC and the radio queue catch up. Unpumped
    /// jack output is dropped — an unplugged jack transmits into open air.
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

    /// Enough HELLO cycles for a fresh cable to reach CONNECTED and for the head
    /// MAC (and any ring closure it implies) to propagate the length of the chain.
    void settleLinks() {
        for (size_t round = 0; round < nodes.size() + 8; ++round) {
            pumpHelloCycle();
            fakeClock->advance(RemoteDeviceCoordinator::HELLO_CADENCE_MS);
        }
    }

    /// Ring closure the way production delivers it: the head's RDC callback, the
    /// roster broadcast, then each member's Idle -> ShootoutProposal mount.
    ///
    /// The head's roster is injected because this fixture drives the legacy
    /// serial handshake, which never latches the RDC ring that would serve one.
    void claimRingOn(size_t headIndex) {
        std::vector<std::array<uint8_t, 6>> roster;
        for (auto& n : nodes) {
            std::array<uint8_t, 6> mac;
            memcpy(mac.data(), n->mac, 6);
            roster.push_back(mac);
        }
        nodes[headIndex]->shootout->setLoopMembersForTest(roster);
        nodes[headIndex]->shootout->onRingClosed();
        deliverAllPackets();
        for (auto& n : nodes) {
            if (n->shootout->shouldEnterProposal()) n->shootout->startProposal();
        }
    }

    // Advance clock on all devices lockstep.
    void advanceClock(unsigned long ms) {
        fakeClock->advance(ms);
    }

    void syncAll() {
        pumpHelloCycle();
        for (auto& n : nodes) {
            n->cdm->sync();
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

    /// Hands every node the ring's member list. Ring detection is local and
    /// real here; the member list is not — it lives on the head only, and no
    /// coordinator broadcast (#169) hands it to followers yet.
    void seedRingRoster() {
        std::vector<std::array<uint8_t, 6>> members;
        for (auto& n : nodes) {
            std::array<uint8_t, 6> mac;
            memcpy(mac.data(), n->mac, 6);
            members.push_back(mac);
        }
        for (auto& n : nodes)
            n->shootout->setLoopMembersForTest(members);
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

        MultiDeviceNode& target = *nodes[toIdx];
        MultiDeviceNode& source = *nodes[p.fromIndex];

        PeerCommsInterface::PacketCallback handler = nullptr;
        void* ctx = nullptr;
        switch (p.type) {
            case PktType::kPdnConnectionContext:
                handler = target.contextHandler;
                ctx = target.contextCtx;
                break;
            case PktType::kRoleAnnounce:         handler = target.roleAnnounceHandler;     ctx = target.roleAnnounceCtx; break;
            case PktType::kRoleAnnounceAck:      handler = target.roleAnnounceAckHandler;  ctx = target.roleAnnounceAckCtx; break;
            case PktType::kChainConfirm:         handler = target.chainConfirmHandler;     ctx = target.chainConfirmCtx; break;
            case PktType::kChainJoin:
                handler = target.chainJoinHandler;
                ctx = target.chainJoinCtx;
                break;
            case PktType::kChainGameEvent:       handler = target.chainGameEventHandler;   ctx = target.chainGameEventCtx; break;
            case PktType::kShootoutCommand:      handler = target.shootoutHandler;         ctx = target.shootoutCtx; break;
            case PktType::kShootoutCommandAck:   handler = target.shootoutAckHandler;      ctx = target.shootoutAckCtx; break;
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
        ON_CALL(*pc, setPacketHandler(testing::Eq(PktType::kPdnConnectionContext), _, _))
            .WillByDefault([&n](PktType, PeerCommsInterface::PacketCallback cb, void* ctx) {
                n.contextHandler = cb;
                n.contextCtx = ctx;
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
        ON_CALL(*pc, setPacketHandler(testing::Eq(PktType::kChainJoin), _, _))
            .WillByDefault([&n](PktType, PeerCommsInterface::PacketCallback cb, void* ctx) {
                n.chainJoinHandler = cb;
                n.chainJoinCtx = ctx;
            });
        ON_CALL(*pc, setPacketHandler(testing::Eq(PktType::kChainGameEvent), _, _))
            .WillByDefault([&n](PktType, PeerCommsInterface::PacketCallback cb, void* ctx) {
                n.chainGameEventHandler = cb; n.chainGameEventCtx = ctx;
            });
        ON_CALL(*pc, setPacketHandler(testing::Eq(PktType::kShootoutCommand), _, _))
            .WillByDefault([&n](PktType, PeerCommsInterface::PacketCallback cb, void* ctx) {
                n.shootoutHandler = cb; n.shootoutCtx = ctx;
            });
        ON_CALL(*pc, setPacketHandler(testing::Eq(PktType::kShootoutCommandAck), _, _))
            .WillByDefault([&n](PktType, PeerCommsInterface::PacketCallback cb, void* ctx) {
                n.shootoutAckHandler = cb; n.shootoutAckCtx = ctx;
            });
    }

    // Mirrors what the GameSession constructor installs for the chain-duel packet
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
            PktType::kChainJoin,
            [](const uint8_t* fromMac, const uint8_t* data, const size_t dataLen, void* ctx) {
                if (dataLen != sizeof(ChainJoinPayload)) return;
                const ChainJoinPayload* p = reinterpret_cast<const ChainJoinPayload*>(data);
                static_cast<ChainDuelManager*>(ctx)->onChainJoinReceived(fromMac, p->championMac);
            },
            cdm);

        // GameSession fans this one to two consumers; only the manager half has a
        // counterpart here, since the fixture stands up no states. Dropping it
        // would leave a supporter's spent press standing into the next round.
        // The champion gate has to be mirrored too: without it a foreign chain's
        // broadcast clears confirmSent here where hardware discards the frame.
        n.device->wirelessManager->setEspNowPacketHandler(
            PktType::kChainGameEvent,
            [](const uint8_t*, const uint8_t* data, const size_t dataLen, void* ctx) {
                if (dataLen != sizeof(ChainGameEventPayload)) return;
                const ChainGameEventPayload* p = reinterpret_cast<const ChainGameEventPayload*>(data);
                ChainDuelManager* manager = static_cast<ChainDuelManager*>(ctx);
                if (!manager->isEventFromOwnChampion(p->championMac)) return;
                manager->onChainGameEventReceived(p->event_type);
            },
            cdm);
    }

    // Register Shootout command + ack handlers. Mirrors the dispatcher in
    // the GameSession constructor so every packet path exercised on hardware is
    // exercised in the fixture too.
    void wireShootoutHandlers(MultiDeviceNode& n) {
        ShootoutManager* mgr = n.shootout.get();

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
                    case ShootoutCmd::CONFIRM: {
                        if (payloadLen < 6) break;
                        const char* name = (payloadLen >= 6 + ShootoutManager::kNameLength)
                            ? reinterpret_cast<const char*>(payload + 6) : nullptr;
                        m->onConfirmReceived(payload, name);
                        break;
                    }
                    case ShootoutCmd::BRACKET:
                    case ShootoutCmd::RING_CLOSED: {
                        if (payloadLen < 1) break;
                        uint8_t count = payload[0];
                        if (payloadLen < 1 + 6u * static_cast<size_t>(count)) break;
                        std::vector<std::array<uint8_t, 6>> macs;
                        for (uint8_t i = 0; i < count; i++) {
                            std::array<uint8_t, 6> mac;
                            memcpy(mac.data(), payload + 1 + 6 * i, 6);
                            macs.push_back(mac);
                        }
                        if (cmd == ShootoutCmd::BRACKET) {
                            m->onBracketReceived(fromMac, macs, seqId);
                        } else {
                            m->onRingClosedReceived(fromMac, macs);
                        }
                        break;
                    }
                    case ShootoutCmd::MATCH_START:
                        if (payloadLen >= 13) m->onMatchStartReceived(payload, payload + 6, payload[12], seqId);
                        break;
                    case ShootoutCmd::MATCH_RESULT:
                        if (payloadLen >= 13) m->onMatchResultReceived(payload, payload + 6, payload[12], seqId, fromMac);
                        break;
                    case ShootoutCmd::TOURNAMENT_END:
                        if (payloadLen >= 6) m->onTournamentEndReceived(payload, seqId);
                        break;
                    case ShootoutCmd::PEER_LOST:
                        if (payloadLen >= 6) m->onPeerLostReceived(payload);
                        break;
                    case ShootoutCmd::ABORT:
                        m->onAbortReceived(fromMac);
                        break;
                }
            },
            mgr);

        n.device->wirelessManager->setEspNowPacketHandler(
            PktType::kShootoutCommandAck,
            [](const uint8_t* fromMac, const uint8_t* data, const size_t dataLen, void* ctx) {
                auto* m = static_cast<ShootoutManager*>(ctx);
                if (dataLen < 2) return;
                ShootoutCmd cmd = static_cast<ShootoutCmd>(data[0]);
                uint8_t seqId = data[1];
                switch (cmd) {
                    case ShootoutCmd::BRACKET:         m->onBracketAckReceived(fromMac, seqId); break;
                    case ShootoutCmd::MATCH_START:     m->onMatchStartAckReceived(fromMac, seqId); break;
                    case ShootoutCmd::MATCH_RESULT: m->onMatchResultAckReceived(fromMac, seqId); break;
                    case ShootoutCmd::TOURNAMENT_END:  m->onTournamentEndAckReceived(fromMac, seqId); break;
                    default: break;
                }
            },
            mgr);
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
    // explicitly to match what GameSession does.
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

    // Both supporters should have learned d0's MAC as championMac.
    ASSERT_NE(d1.cdm->getChampionMac(), nullptr);
    EXPECT_EQ(memcmp(d1.cdm->getChampionMac(), d0.mac, 6), 0);
    ASSERT_NE(d2.cdm->getChampionMac(), nullptr);
    EXPECT_EQ(memcmp(d2.cdm->getChampionMac(), d0.mac, 6), 0);
}

// H-H-H linear chain: the champion counts the supporter two cables away as
// readily as the one on its own cable. The far one is placed by the join it
// sent when the role cascade named its champion, nothing else can see it.
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

    // D2 is two cables from the champion and shares none of them with it.
    d2.cdm->sendConfirm();
    suite->deliverAllPackets();

    EXPECT_EQ(d0.cdm->getConfirmedSupporterCount(), 1u);
    EXPECT_EQ(d0.cdm->getBoostMs(), ChainDuelManager::BOOST_PER_SUPPORTER_MS);

    // D1 is on the champion's own cable, so its confirm counts too.
    d1.cdm->sendConfirm();
    suite->deliverAllPackets();

    EXPECT_EQ(d0.cdm->getConfirmedSupporterCount(), 2u);
    EXPECT_EQ(d0.cdm->getBoostMs(), 2 * ChainDuelManager::BOOST_PER_SUPPORTER_MS);
}

// Boost is the reason the chain exists: it has to grow with the chain, not sit
// at one supporter's worth however many devices are plugged in behind it. Four
// hunters, three supporters at one, two and three cables of distance, all three
// counted and all three paid.
inline void cdmMultiDeviceBoostScalesWithChainDepth(ChainDuelMultiDeviceFixture* suite) {
    suite->spawnDevices(4);
    suite->setAllHunters();
    suite->connectLinearHunterChain();
    suite->deliverAllPackets();
    suite->syncAll();
    suite->deliverAllPackets();

    // Two passes: the cascade advances one cable per pass, so the tail needs the
    // second to learn the champion and announce itself back.
    for (int pass = 0; pass < 3; ++pass) {
        for (size_t i = 0; i < suite->nodeCount(); ++i) {
            suite->node(i).cdm->onChainStateChanged();
        }
        suite->deliverAllPackets();
    }

    MultiDeviceNode& champion = suite->node(0);
    ASSERT_TRUE(champion.cdm->isChampion());
    for (size_t i = 1; i < suite->nodeCount(); ++i) {
        ASSERT_NE(suite->node(i).cdm->getChampionMac(), nullptr) << "node " << i;
        ASSERT_EQ(memcmp(suite->node(i).cdm->getChampionMac(), champion.mac, 6), 0)
            << "node " << i << " follows the wrong champion";
    }
    // Every supporter is on the roster before a single press, which is what lets
    // a COUNTDOWN reach a device that has not confirmed yet.
    ASSERT_EQ(champion.cdm->getSupporterChainPeers().size(), 3u);

    for (size_t i = 1; i < suite->nodeCount(); ++i) {
        suite->node(i).cdm->sendConfirm();
        suite->deliverAllPackets();
    }

    EXPECT_EQ(champion.cdm->getConfirmedSupporterCount(), 3u);
    EXPECT_EQ(champion.cdm->getBoostMs(), 3 * ChainDuelManager::BOOST_PER_SUPPORTER_MS);
}

// The champion's COUNTDOWN/WIN/LOSS have to land on every supporter, not just
// the one it shares a cable with — a supporter three cables away never sees a
// round start otherwise, and never leaves its stale screen.
inline void cdmMultiDeviceGameEventReachesDistantSupporter(ChainDuelMultiDeviceFixture* suite) {
    suite->spawnDevices(4);
    suite->setAllHunters();
    suite->connectLinearHunterChain();
    suite->deliverAllPackets();
    suite->syncAll();
    suite->deliverAllPackets();

    for (int pass = 0; pass < 3; ++pass) {
        for (size_t i = 0; i < suite->nodeCount(); ++i) {
            suite->node(i).cdm->onChainStateChanged();
        }
        suite->deliverAllPackets();
    }

    MultiDeviceNode& champion = suite->node(0);
    ASSERT_TRUE(champion.cdm->isChampion());

    // Count the events each node accepts as its own champion's, which is the
    // filter Quickdraw::onChainGameEventPacket applies on hardware.
    std::vector<int> accepted(suite->nodeCount(), 0);
    for (size_t i = 0; i < suite->nodeCount(); ++i) {
        MultiDeviceNode& n = suite->node(i);
        int* counter = &accepted[i];
        n.device->wirelessManager->setEspNowPacketHandler(
            PktType::kChainGameEvent,
            [](const uint8_t*, const uint8_t* data, const size_t dataLen, void* ctx) {
                if (dataLen != sizeof(ChainGameEventPayload)) return;
                static_cast<int*>(ctx)[0]++;
            },
            counter);
    }

    champion.cdm->sendGameEventToSupporters(ChainGameEventType::COUNTDOWN);
    suite->deliverAllPackets();

    EXPECT_EQ(accepted[0], 0) << "champion should not receive its own broadcast";
    for (size_t i = 1; i < suite->nodeCount(); ++i) {
        EXPECT_EQ(accepted[i], 1) << "node " << i << " missed the COUNTDOWN";
    }

    // Every supporter, at every depth, filters on the champion the event names.
    for (size_t i = 1; i < suite->nodeCount(); ++i) {
        EXPECT_TRUE(suite->node(i).cdm->isEventFromOwnChampion(champion.mac))
            << "node " << i << " would reject its own champion's event";
    }
    uint8_t strangerChampion[6] = {0x77, 0x77, 0x77, 0x77, 0x77, 0x77};
    EXPECT_FALSE(suite->node(3).cdm->isEventFromOwnChampion(strangerChampion));
}

// End-to-end Shootout consensus: 4 devices form a ring, each confirms,
// every device reaches BRACKET_REVEAL phase, coordinator broadcasts
// MATCH_START and both duelists transition into MATCH_IN_PROGRESS.
inline void shootoutFourDeviceConsensusAndMatchStart(ChainDuelMultiDeviceFixture* suite) {
    suite->spawnDevices(4);
    suite->setAllHunters();
    suite->connectLinearHunterChain();
    suite->deliverAllPackets();
    suite->syncAll();
    suite->deliverAllPackets();
    suite->closeRing();

    // Every device should now see the ring as closed.
    for (size_t i = 0; i < suite->nodeCount(); ++i) {
        ASSERT_TRUE(suite->node(i).cdm->isLoop())
            << "node " << i << " does not see loop";
    }

    suite->claimRingOn(0);
    for (size_t i = 0; i < suite->nodeCount(); ++i) {
        EXPECT_EQ(suite->node(i).shootout->getPhase(), ShootoutManager::Phase::PROPOSAL)
            << "node " << i << " missed the ring-closed broadcast";
    }
    EXPECT_TRUE(suite->node(0).shootout->isCoordinator());

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

    // Exactly one coordinator. Coordinator should be MATCH_IN_PROGRESS.
    // Two duelists should also be MATCH_IN_PROGRESS; spectators too
    // (phase is per-device, and receiving MATCH_START sets phase on
    // everyone).
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

// Drive a 4-device tournament from ring closure through TOURNAMENT_END. Covers
// advance-round logic: expected match count is 3 (2 round-1 matches, 1 final).
// Guards against regression where round advances after only 1 match or stalls
// between matches.
inline void shootoutFourDeviceFullTournament(ChainDuelMultiDeviceFixture* suite) {
    suite->spawnDevices(4);
    suite->setAllHunters();
    suite->connectLinearHunterChain();
    suite->deliverAllPackets();
    suite->syncAll();
    suite->deliverAllPackets();
    suite->closeRing();

    suite->claimRingOn(0);
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

    auto localDuelistIndex = [&]() -> int {
        for (size_t i = 0; i < suite->nodeCount(); ++i) {
            if (suite->node(i).shootout->isLocalDuelist()) return (int)i;
        }
        return -1;
    };

    // Drive up to 4 matches (safety cap). Real count should be 3.
    int matchesDriven = 0;
    for (int m = 0; m < 6; ++m) {
        int duelistIdx = localDuelistIndex();
        if (duelistIdx < 0) break;
        // Have this duelist report a win. The other flow (opponent wins) is
        // symmetric; picking one side keeps the test deterministic.
        suite->node(duelistIdx).shootout->reportLocalWin();
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
        EXPECT_EQ(phase, ShootoutManager::Phase::ENDED)
            << "node " << i << " phase not ENDED";
        auto wArr = suite->node(i).shootout->getTournamentWinner();
        if (!winnerSet) { winner = wArr; winnerSet = true; }
        else {
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
inline void shootoutEightDeviceFullTournament(ChainDuelMultiDeviceFixture* suite) {
    suite->spawnDevices(8);
    suite->setAllHunters();
    suite->connectLinearHunterChain();
    suite->deliverAllPackets();
    suite->syncAll();
    suite->deliverAllPackets();
    suite->closeRing();

    static const char* kNames[8] = {
        "alice", "bob", "carol", "dave", "erin", "frank", "gina", "hank"
    };
    for (size_t i = 0; i < suite->nodeCount(); ++i) {
        suite->node(i).player->setName(kNames[i]);
    }

    suite->claimRingOn(0);
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
    EXPECT_EQ(matches, 7) << "8-device SE bracket should run exactly 7 matches";
    for (size_t i = 0; i < suite->nodeCount(); ++i) {
        EXPECT_EQ(suite->node(i).shootout->getPhase(), ShootoutManager::Phase::ENDED)
            << "8-device node " << i;
    }

    // Every node should know every other node's name after CONFIRM propagation.
    for (size_t observer = 0; observer < suite->nodeCount(); ++observer) {
        for (size_t target = 0; target < suite->nodeCount(); ++target) {
            std::string resolved =
                suite->node(observer).shootout->getNameForMac(
                    suite->node(target).mac);
            EXPECT_EQ(resolved, kNames[target])
                << "node " << observer << " can't resolve node " << target;
        }
    }
}

// Two tournaments on one ring, with a reset between them. The ring has to
// survive the whole first tournament, including its bracket-reveal wait, or the
// second one has no members to bracket.
inline void shootoutFourDeviceTwoTournamentsBackToBack(ChainDuelMultiDeviceFixture* suite) {
    suite->spawnDevices(4);
    suite->setAllHunters();
    suite->connectLinearHunterChain();
    suite->deliverAllPackets();
    suite->syncAll();
    suite->deliverAllPackets();
    suite->closeRing();

    auto runOne = [&]() {
        suite->claimRingOn(0);
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
                if (suite->node(i).shootout->isLocalDuelist()) {
                    duelistIdx = (int)i;
                    break;
                }
            }
            if (duelistIdx < 0) break;
            suite->node(duelistIdx).shootout->reportLocalWin();
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

    // CDM loop must still be intact for tournament 2.
    for (size_t i = 0; i < suite->nodeCount(); ++i) {
        ASSERT_TRUE(suite->node(i).cdm->isLoop())
            << "post-reset loop broken on node " << i;
    }

    // Run tournament 2. runOne re-claims the ring, which is what re-seeds the
    // loop-member set that resetToIdle just dropped.
    int matchesTwo = runOne();
    for (size_t i = 0; i < suite->nodeCount(); ++i) {
        ASSERT_EQ(suite->node(i).shootout->getLoopMembers().size(), 4u)
            << "loop members wrong on node " << i;
    }
    EXPECT_EQ(matchesTwo, 3) << "tournament 2 did not run 3 matches";
    for (size_t i = 0; i < suite->nodeCount(); ++i) {
        EXPECT_EQ(suite->node(i).shootout->getPhase(), ShootoutManager::Phase::ENDED)
            << "tourn 2 node " << i;
    }
}
