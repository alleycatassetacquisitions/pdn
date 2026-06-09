#pragma once

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "device-mock.hpp"
#include "utility-tests.hpp"
#include "device/remote-device-coordinator.hpp"
#include "device/peer-graph-codec.hpp"

using ::testing::Return;
using ::testing::_;

// ============================================
// RemoteDeviceCoordinator Tests (peer-graph protocol)
// ============================================

class RDCTests : public testing::Test {
public:
    void SetUp() override {
        fakeClock = new FakePlatformClock();
        SimpleTimer::setPlatformClock(fakeClock);
        fakeClock->setTime(1000);

        ON_CALL(*device.mockPeerComms, sendData(_, _, _, _)).WillByDefault(Return(1));
        ON_CALL(*device.mockPeerComms, getMacAddress()).WillByDefault(Return(localMac));
        ON_CALL(*device.mockPeerComms, addEspNowPeer(_)).WillByDefault(Return(0));
        ON_CALL(*device.mockPeerComms, removeEspNowPeer(_)).WillByDefault(Return(0));
        ON_CALL(*device.mockPeerComms, getPeerCommsState()).WillByDefault(Return(PeerCommsState::CONNECTED));

        rdc.initialize(device.wirelessManager, device.serialManager, &device);
        // Most single-device tests advance the fake clock across windows far
        // longer than the 30ms HELLO silent-link. Push the threshold out so
        // silent-link doesn't fire unless a test specifically exercises it.
        rdc.setJackDeadSilentLinkMsForTest(60000);
    }

    void TearDown() override {
        SimpleTimer::setPlatformClock(nullptr);
        delete fakeClock;
    }

    FakeHWSerialWrapper& serialFor(SerialIdentifier jack) {
        return jack == SerialIdentifier::INPUT_JACK
            ? device.inputJackSerial : device.outputJackSerial;
    }

    void deliverHello(SerialIdentifier jack, const uint8_t* mac,
                      uint8_t deviceType = static_cast<uint8_t>(DeviceType::PDN)) {
        net::Mac m;
        std::copy_n(mac, 6, m.begin());
        auto frame = peer_graph::encodeHello(m, deviceType, peer_graph::kUserIdUnknown);
        serialFor(jack).bytesCallback(frame.data(), frame.size());
    }

    void deliverBeacon(SerialIdentifier jack, const BeaconRecord& b) {
        auto frame = peer_graph::encodeBeacon(b);
        serialFor(jack).bytesCallback(frame.data(), frame.size());
        // ingestSerial only enqueues; exec() drains it so the BEACON is accepted
        // and flooded within this helper, without driving a full sync().
        rdc.exec();
    }

    // True if a BEACON frame from `source` claiming (inPeer, outPeer) appears in
    // the bytes RDC emitted on `jack` since the last queue drain.
    bool emittedBeacon(SerialIdentifier jack, const BeaconRecord& want) {
        auto& q = serialFor(jack).msgQueue;
        std::vector<uint8_t> bytes(q.begin(), q.end());
        const auto target = peer_graph::encodeBeacon(want);
        if (bytes.size() < target.size()) return false;
        for (size_t i = 0; i + target.size() <= bytes.size(); ++i) {
            if (std::equal(target.begin(), target.end(), bytes.begin() + i)) return true;
        }
        return false;
    }

    void drainQueues() {
        device.inputJackSerial.msgQueue.clear();
        device.outputJackSerial.msgQueue.clear();
    }

    net::Mac mac(uint8_t last) { return {0x0A, 0, 0, 0, 0, last}; }

    MockDevice device;
    RemoteDeviceCoordinator rdc;
    FakePlatformClock* fakeClock;
    uint8_t localMac[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
};

inline void rdcDefaultStateIsDisconnected(RDCTests* suite) {
    EXPECT_EQ(suite->rdc.getPortStatus(SerialIdentifier::OUTPUT_JACK), PortStatus::DISCONNECTED);
    EXPECT_EQ(suite->rdc.getPortStatus(SerialIdentifier::INPUT_JACK), PortStatus::DISCONNECTED);
    EXPECT_EQ(suite->rdc.getPeerMac(SerialIdentifier::OUTPUT_JACK), nullptr);
    EXPECT_FALSE(suite->rdc.isInLoop());
    EXPECT_EQ(suite->rdc.getChainMembers().size(), 1u);  // just self
}

inline void rdcHelloSetsMacPeerAndConnected(RDCTests* suite) {
    uint8_t peer[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    suite->deliverHello(SerialIdentifier::OUTPUT_JACK, peer);
    suite->rdc.sync();
    EXPECT_EQ(suite->rdc.getPortStatus(SerialIdentifier::OUTPUT_JACK), PortStatus::CONNECTED);
    const uint8_t* m = suite->rdc.getPeerMac(SerialIdentifier::OUTPUT_JACK);
    ASSERT_NE(m, nullptr);
    EXPECT_EQ(m[0], 0xAA);
    EXPECT_EQ(m[5], 0xFF);
    // INPUT untouched.
    EXPECT_EQ(suite->rdc.getPortStatus(SerialIdentifier::INPUT_JACK), PortStatus::DISCONNECTED);
}

inline void rdcHelloFromSelfRejected(RDCTests* suite) {
    suite->deliverHello(SerialIdentifier::OUTPUT_JACK, suite->localMac);
    suite->rdc.sync();
    EXPECT_EQ(suite->rdc.getPeerMac(SerialIdentifier::OUTPUT_JACK), nullptr);
}

inline void rdcSilentLinkClearsPeerAfterThreshold(RDCTests* suite) {
    suite->rdc.setJackDeadSilentLinkMsForTest(30);
    uint8_t peer[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    suite->deliverHello(SerialIdentifier::OUTPUT_JACK, peer);
    suite->rdc.sync();
    ASSERT_NE(suite->rdc.getPeerMac(SerialIdentifier::OUTPUT_JACK), nullptr);
    // No further HELLO; advance past 30ms.
    suite->fakeClock->advance(40);
    suite->rdc.sync();
    EXPECT_EQ(suite->rdc.getPeerMac(SerialIdentifier::OUTPUT_JACK), nullptr);
    EXPECT_EQ(suite->rdc.getPortStatus(SerialIdentifier::OUTPUT_JACK), PortStatus::DISCONNECTED);
}

// A peer that silent-dies must release its ESP-NOW slot. The 20-slot table is
// finite; without this every distinct neighbour that drops leaks a slot until
// new peers are rejected and matches silently fail.
inline void rdcSilentLinkReleasesEspNowPeerSlot(RDCTests* suite) {
    suite->rdc.setJackDeadSilentLinkMsForTest(30);
    uint8_t peer[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    suite->deliverHello(SerialIdentifier::OUTPUT_JACK, peer);
    suite->rdc.sync();
    ASSERT_NE(suite->rdc.getPeerMac(SerialIdentifier::OUTPUT_JACK), nullptr);

    EXPECT_CALL(*suite->device.mockPeerComms,
                removeEspNowPeer(::testing::Truly([&](const uint8_t* m) {
                    return m != nullptr && memcmp(m, peer, 6) == 0;
                }))).Times(1);

    // No further HELLO; silent-link fires declareJackDead on the next sync.
    suite->fakeClock->advance(40);
    suite->rdc.sync();
}

inline void rdcSilentLinkSurvivesRefreshWithinWindow(RDCTests* suite) {
    // 100ms mirrors the production kHelloSilentLinkMs. Each HELLO inside the
    // window resets the liveness baseline, so a peer that keeps emitting never
    // trips the watchdog; only a full window of true silence declares it dead.
    // Guards the false-fire margin: a sub-threshold gap must not drop the peer.
    suite->rdc.setJackDeadSilentLinkMsForTest(100);
    uint8_t peer[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    suite->deliverHello(SerialIdentifier::OUTPUT_JACK, peer);
    suite->rdc.sync();
    ASSERT_NE(suite->rdc.getPeerMac(SerialIdentifier::OUTPUT_JACK), nullptr);

    // 80ms gap (< window), then a refreshing HELLO: baseline resets, still alive.
    suite->fakeClock->advance(80);
    suite->deliverHello(SerialIdentifier::OUTPUT_JACK, peer);
    suite->rdc.sync();
    EXPECT_NE(suite->rdc.getPeerMac(SerialIdentifier::OUTPUT_JACK), nullptr);

    // Another 80ms gap, still under 100ms since the last HELLO: alive.
    suite->fakeClock->advance(80);
    suite->rdc.sync();
    EXPECT_NE(suite->rdc.getPeerMac(SerialIdentifier::OUTPUT_JACK), nullptr);

    // Now go fully silent past the window: dead.
    suite->fakeClock->advance(120);
    suite->rdc.sync();
    EXPECT_EQ(suite->rdc.getPeerMac(SerialIdentifier::OUTPUT_JACK), nullptr);
}

inline void rdcSilentLinkFiresDisconnectCallback(RDCTests* suite) {
    suite->rdc.setJackDeadSilentLinkMsForTest(30);
    int disconnects = 0;
    suite->rdc.setOnDirectPeerChange(
        [&](SerialIdentifier, std::optional<RemoteDeviceCoordinator::Peer> prev,
            std::optional<RemoteDeviceCoordinator::Peer> cur) {
            if (prev.has_value() && !cur.has_value()) disconnects++;
        });
    uint8_t peer[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    suite->deliverHello(SerialIdentifier::OUTPUT_JACK, peer);
    suite->rdc.sync();
    suite->fakeClock->advance(40);
    suite->rdc.sync();
    EXPECT_EQ(disconnects, 1);
}

inline void rdcConnectFiresConnectCallback(RDCTests* suite) {
    int connects = 0;
    suite->rdc.setOnDirectPeerChange(
        [&](SerialIdentifier, std::optional<RemoteDeviceCoordinator::Peer> prev,
            std::optional<RemoteDeviceCoordinator::Peer> cur) {
            if (!prev.has_value() && cur.has_value()) connects++;
        });
    uint8_t peer[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    suite->deliverHello(SerialIdentifier::OUTPUT_JACK, peer);
    suite->rdc.sync();
    EXPECT_EQ(connects, 1);
}

// removeMacPeer still holds the dropped peer's full record, so the disconnect
// callback carries the real deviceType rather than an UNKNOWN placeholder.
inline void rdcDisconnectCallbackCarriesDeviceType(RDCTests* suite) {
    suite->rdc.setJackDeadSilentLinkMsForTest(30);
    int disconnects = 0;
    DeviceType droppedType = DeviceType::UNKNOWN;
    suite->rdc.setOnDirectPeerChange(
        [&](SerialIdentifier, std::optional<RemoteDeviceCoordinator::Peer> prev,
            std::optional<RemoteDeviceCoordinator::Peer> cur) {
            if (prev.has_value() && !cur.has_value()) {
                disconnects++;
                droppedType = prev->deviceType;
            }
        });
    uint8_t peer[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    suite->deliverHello(SerialIdentifier::OUTPUT_JACK, peer,
                        static_cast<uint8_t>(DeviceType::FDN));
    suite->rdc.sync();
    suite->fakeClock->advance(40);
    suite->rdc.sync();
    EXPECT_EQ(disconnects, 1);
    EXPECT_EQ(droppedType, DeviceType::FDN);
}

// Edge detection lives in DirectPeerTable (setMacPeer fires only on an empty->present
// transition): the repeated HELLOs of a steady connection must not re-fire
// connect every sync.
inline void rdcRepeatedHelloFiresConnectOnce(RDCTests* suite) {
    int connects = 0;
    suite->rdc.setOnDirectPeerChange(
        [&](SerialIdentifier, std::optional<RemoteDeviceCoordinator::Peer> prev,
            std::optional<RemoteDeviceCoordinator::Peer> cur) {
            if (!prev.has_value() && cur.has_value()) connects++;
        });
    uint8_t peer[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    for (int i = 0; i < 3; ++i) {
        suite->deliverHello(SerialIdentifier::OUTPUT_JACK, peer);
        suite->rdc.sync();
    }
    EXPECT_EQ(connects, 1);
}

inline void rdcIsDirectPeerTrueForCableNeighbor(RDCTests* suite) {
    uint8_t peer[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    suite->deliverHello(SerialIdentifier::INPUT_JACK, peer);
    suite->rdc.sync();
    EXPECT_TRUE(suite->rdc.isDirectPeer(peer));
    uint8_t stranger[6] = {0x99, 0, 0, 0, 0, 0};
    EXPECT_FALSE(suite->rdc.isDirectPeer(stranger));
}

inline void rdcMacPeerChangeEmitsBeacon(RDCTests* suite) {
    suite->drainQueues();
    uint8_t peer[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    suite->deliverHello(SerialIdentifier::OUTPUT_JACK, peer);
    suite->rdc.sync();
    // reconcileSelfPeers should have emitted a BEACON claiming the peer on OUT.
    BeaconRecord want;
    std::copy_n(suite->localMac, 6, want.source.begin());
    std::copy_n(peer, 6, want.outPeer.begin());  // inPeer stays zero
    EXPECT_TRUE(suite->emittedBeacon(SerialIdentifier::OUTPUT_JACK, want));
    EXPECT_TRUE(suite->emittedBeacon(SerialIdentifier::INPUT_JACK, want));
}

// A 3-device ring: self + two peers each claiming around the ring → isInLoop.
inline void rdcBeaconsFormRingIsInLoop(RDCTests* suite) {
    // Self's direct peers: 0x0A..02 on OUTPUT, 0x0A..03 on INPUT.
    suite->deliverHello(SerialIdentifier::OUTPUT_JACK, suite->mac(0x02).data());
    suite->deliverHello(SerialIdentifier::INPUT_JACK, suite->mac(0x03).data());
    suite->rdc.sync();
    // Peer 0x02 claims self (on its IN) and 0x03 (on its OUT).
    net::Mac self;
    std::copy_n(suite->localMac, 6, self.begin());
    suite->deliverBeacon(SerialIdentifier::OUTPUT_JACK,
                         {suite->mac(0x02), suite->mac(0x03), self});
    // Peer 0x03 claims 0x02 (IN) and self (OUT).
    suite->deliverBeacon(SerialIdentifier::INPUT_JACK,
                         {suite->mac(0x03), suite->mac(0x02), self});
    suite->rdc.sync();
    EXPECT_TRUE(suite->rdc.isInLoop());
    EXPECT_EQ(suite->rdc.getChainMembers().size(), 3u);
}

inline void rdcSelfSourcedBeaconNotForwarded(RDCTests* suite) {
    suite->drainQueues();
    net::Mac self;
    std::copy_n(suite->localMac, 6, self.begin());
    // A beacon claiming to originate from self arrives (our own, looped around).
    suite->deliverBeacon(SerialIdentifier::OUTPUT_JACK,
                         {self, suite->mac(0x02), suite->mac(0x03)});
    // It must not be re-emitted on the opposite (INPUT) jack.
    auto& inQ = suite->device.inputJackSerial.msgQueue;
    std::vector<uint8_t> bytes(inQ.begin(), inQ.end());
    BeaconRecord echoed{self, suite->mac(0x02), suite->mac(0x03)};
    auto target = peer_graph::encodeBeacon(echoed);
    bool found = false;
    for (size_t i = 0; i + target.size() <= bytes.size(); ++i)
        if (std::equal(target.begin(), target.end(), bytes.begin() + i)) found = true;
    EXPECT_FALSE(found);
}

inline void rdcForeignBeaconFloodedOnOppositeJack(RDCTests* suite) {
    suite->drainQueues();
    BeaconRecord b{suite->mac(0x02), suite->mac(0x03), suite->mac(0x04)};
    suite->deliverBeacon(SerialIdentifier::OUTPUT_JACK, b);
    // Flooded onward on INPUT (opposite jack).
    auto& inQ = suite->device.inputJackSerial.msgQueue;
    std::vector<uint8_t> bytes(inQ.begin(), inQ.end());
    auto target = peer_graph::encodeBeacon(b);
    bool found = false;
    for (size_t i = 0; i + target.size() <= bytes.size(); ++i)
        if (std::equal(target.begin(), target.end(), bytes.begin() + i)) found = true;
    EXPECT_TRUE(found);
}

// A forwarded BEACON lost to line noise is never re-delivered by the
// change-gated flood, so the 1Hz backstop must replay the cached beacons of
// current chain members, not just self. Departed nodes (no mutual path to
// self) stay quiet so replay cost tracks live chain size, not event history.
// Replay is paced, so the test pumps sync across the pacing interval.
inline void rdcBackstopReplaysChainMemberBeacons(RDCTests* suite) {
    net::Mac self;
    std::copy_n(suite->localMac, 6, self.begin());
    // Linear chain: self -OUTPUT- 0x02 - 0x03 (open end). INPUT stays open.
    suite->deliverHello(SerialIdentifier::OUTPUT_JACK, suite->mac(0x02).data());
    suite->rdc.sync();
    BeaconRecord member2{suite->mac(0x02), self, suite->mac(0x03)};
    BeaconRecord member3{suite->mac(0x03), suite->mac(0x02), {}};
    suite->deliverBeacon(SerialIdentifier::OUTPUT_JACK, member2);
    suite->deliverBeacon(SerialIdentifier::OUTPUT_JACK, member3);
    // A departed node's beacon: cached, but no mutual edge into our component.
    BeaconRecord zombie{suite->mac(0x07), suite->mac(0x08), suite->mac(0x09)};
    suite->deliverBeacon(SerialIdentifier::OUTPUT_JACK, zombie);
    suite->rdc.sync();

    suite->drainQueues();
    // Cross the 1s backstop, then pump sync across pacing intervals so the
    // paced queue drains fully.
    suite->fakeClock->advance(1100);
    for (int i = 0; i < 6; ++i) {
        suite->rdc.sync();
        suite->fakeClock->advance(60);
    }

    EXPECT_TRUE(suite->emittedBeacon(SerialIdentifier::OUTPUT_JACK, member2))
        << "backstop must replay a chain member's cached beacon";
    EXPECT_TRUE(suite->emittedBeacon(SerialIdentifier::OUTPUT_JACK, member3))
        << "backstop must replay multi-hop members too";
    EXPECT_FALSE(suite->emittedBeacon(SerialIdentifier::OUTPUT_JACK, zombie))
        << "departed nodes' beacons must not be replayed";
    EXPECT_FALSE(suite->emittedBeacon(SerialIdentifier::INPUT_JACK, member2))
        << "no replay onto a jack with no direct peer";
    EXPECT_FALSE(suite->emittedBeacon(SerialIdentifier::INPUT_JACK, zombie));
}

// Replay frames must trickle out one per jack per kReplayPacingMs, never as a
// back-to-back burst: a burst of N beacons on the 19200-baud jack queues
// ahead of the 20ms HELLO stream and stalls it past the 100ms silent-link
// threshold at N>=7, making the partner declare the jack dead once a second.
inline void rdcBeaconReplayIsPaced(RDCTests* suite) {
    net::Mac self;
    std::copy_n(suite->localMac, 6, self.begin());
    // self -OUTPUT- 0x02, with 0x03 and 0x04 chained beyond it.
    suite->deliverHello(SerialIdentifier::OUTPUT_JACK, suite->mac(0x02).data());
    suite->rdc.sync();
    suite->deliverBeacon(SerialIdentifier::OUTPUT_JACK,
                         {suite->mac(0x02), self, suite->mac(0x03)});
    suite->deliverBeacon(SerialIdentifier::OUTPUT_JACK,
                         {suite->mac(0x03), suite->mac(0x02), suite->mac(0x04)});
    suite->deliverBeacon(SerialIdentifier::OUTPUT_JACK,
                         {suite->mac(0x04), suite->mac(0x03), {}});
    suite->rdc.sync();

    // Cross the backstop: this tick emits the self beacon plus AT MOST one
    // paced replay frame on the peered jack.
    const auto base = suite->rdc.getBeaconStats(SerialIdentifier::OUTPUT_JACK).framesTx;
    suite->fakeClock->advance(1100);
    suite->rdc.sync();
    const auto afterBackstop =
        suite->rdc.getBeaconStats(SerialIdentifier::OUTPUT_JACK).framesTx;
    EXPECT_LE(afterBackstop - base, 2u)
        << "backstop tick must not burst the whole member replay at once";

    // Within the pacing window, another sync emits nothing further.
    suite->fakeClock->advance(10);
    suite->rdc.sync();
    EXPECT_EQ(suite->rdc.getBeaconStats(SerialIdentifier::OUTPUT_JACK).framesTx,
              afterBackstop)
        << "replay must respect the pacing interval";

    // Past the pacing window, exactly one more replay frame goes out.
    suite->fakeClock->advance(60);
    suite->rdc.sync();
    EXPECT_EQ(suite->rdc.getBeaconStats(SerialIdentifier::OUTPUT_JACK).framesTx,
              afterBackstop + 1)
        << "one replay frame per pacing interval";
}

// recvQueue_ is fed by the UART task (HELLO/BEACON) and the 20ms watchdog
// (JACK_SILENT) but drained only by the main loop, so a stalled loop must not
// let it grow without bound on a 512KB part. Shedding the OLDEST entry is
// safe (every kind is periodically regenerated) and keeps the freshest.
inline void rdcRecvQueueBoundedDropsOldest(RDCTests* suite) {
    // Simulate a stalled main loop: enqueue far past the cap with no drain.
    for (size_t i = 0; i < RemoteDeviceCoordinator::kRecvQueueMax + 50; ++i) {
        suite->deliverHello(SerialIdentifier::OUTPUT_JACK, suite->mac(0x02).data());
    }
    EXPECT_EQ(suite->rdc.recvQueueDepthForTest(),
              RemoteDeviceCoordinator::kRecvQueueMax);

    // The newest event must survive the cap: a BEACON arriving last (queue
    // already full) is still accepted and flooded once the loop drains.
    suite->drainQueues();
    BeaconRecord b{suite->mac(0x05), suite->mac(0x06), {}};
    auto frame = peer_graph::encodeBeacon(b);
    suite->serialFor(SerialIdentifier::OUTPUT_JACK)
        .bytesCallback(frame.data(), frame.size());
    suite->rdc.exec();
    EXPECT_TRUE(suite->emittedBeacon(SerialIdentifier::INPUT_JACK, b))
        << "newest event was dropped; cap must shed oldest, not newest";
}

// A different MAC arriving on an occupied jack (cable swapped within the
// silent-link window) is a disconnect of the old peer followed by a connect of
// the new one. Swallowing it silently leaves the old MAC's ESP-NOW slot
// leaked, the new MAC unregistered (reliable sends to it fail), and the game
// layer unaware the partner changed.
inline void rdcSameJackPeerSwapFiresDisconnectThenConnect(RDCTests* suite) {
    std::vector<std::pair<bool, net::Mac>> events;  // {isConnect, mac}
    suite->rdc.setOnDirectPeerChange(
        [&](SerialIdentifier, std::optional<RemoteDeviceCoordinator::Peer> prev,
            std::optional<RemoteDeviceCoordinator::Peer> cur) {
            if (cur.has_value()) {
                net::Mac m;
                std::copy_n(cur->mac.data(), 6, m.begin());
                events.push_back({true, m});
            } else if (prev.has_value()) {
                net::Mac m;
                std::copy_n(prev->mac.data(), 6, m.begin());
                events.push_back({false, m});
            }
        });

    suite->deliverHello(SerialIdentifier::OUTPUT_JACK, suite->mac(0x02).data());
    suite->rdc.sync();
    ASSERT_EQ(events.size(), 1u);

    EXPECT_CALL(*suite->device.mockPeerComms,
                removeEspNowPeer(::testing::Truly([&](const uint8_t* m) {
                    return m != nullptr && memcmp(m, suite->mac(0x02).data(), 6) == 0;
                }))).Times(1);
    EXPECT_CALL(*suite->device.mockPeerComms,
                addEspNowPeer(::testing::Truly([&](const uint8_t* m) {
                    return m != nullptr && memcmp(m, suite->mac(0x05).data(), 6) == 0;
                }))).Times(1);

    // The swap: a different peer's HELLO on the same, still-occupied jack.
    suite->deliverHello(SerialIdentifier::OUTPUT_JACK, suite->mac(0x05).data());
    suite->rdc.sync();

    ASSERT_EQ(events.size(), 3u) << "swap must fire disconnect then connect";
    EXPECT_FALSE(events[1].first);
    EXPECT_EQ(events[1].second, suite->mac(0x02));
    EXPECT_TRUE(events[2].first);
    EXPECT_EQ(events[2].second, suite->mac(0x05));

    const uint8_t* now = suite->rdc.getPeerMac(SerialIdentifier::OUTPUT_JACK);
    ASSERT_NE(now, nullptr);
    EXPECT_EQ(memcmp(now, suite->mac(0x05).data(), 6), 0);
}

// The role byte is BEACON content like the peer slots, so a change must
// re-emit immediately: the beacon keeps remote peers' champion derivation in
// sync and emitBeaconBothJacks() is also what copies the byte into the local
// graph (setSelfRole). Waiting for the 1Hz backstop leaves the local champion
// walk reading a stale role for up to a second.
inline void rdcRoleFlipEmitsBeaconImmediately(RDCTests* suite) {
    uint8_t role = chain_role::byteFor(false);
    suite->rdc.setRoleProvider([&] { return role; });

    suite->deliverHello(SerialIdentifier::OUTPUT_JACK, suite->mac(0x02).data());
    suite->rdc.sync();
    suite->drainQueues();

    // Flip the role with no topology change and no backstop due (clock still).
    role = chain_role::byteFor(true);
    suite->rdc.sync();

    net::Mac self;
    std::copy_n(suite->localMac, 6, self.begin());
    BeaconRecord want{self, {}, suite->mac(0x02)};
    want.role = chain_role::byteFor(true);
    EXPECT_TRUE(suite->emittedBeacon(SerialIdentifier::OUTPUT_JACK, want))
        << "role change must re-emit the BEACON without waiting for the backstop";
}

// The change-gated flood never re-delivers unchanged far beacons to a peer
// that joined or rebooted after convergence, so a new HELLO replays our cache
// out that jack; connect-only, so steady state stays 1Hz.
inline void rdcNewPeerReplaysCachedTopology(RDCTests* suite) {
    // Seed a cached beacon for a node beyond our direct neighbors.
    BeaconRecord far{suite->mac(0x07), suite->mac(0x08), suite->mac(0x09)};
    suite->deliverBeacon(SerialIdentifier::OUTPUT_JACK, far);
    suite->drainQueues();
    // A fresh neighbor connects on INPUT.
    suite->deliverHello(SerialIdentifier::INPUT_JACK, suite->mac(0x03).data());
    suite->rdc.sync();
    EXPECT_TRUE(suite->emittedBeacon(SerialIdentifier::INPUT_JACK, far))
        << "new peer must receive a replay of the cached topology";
}
