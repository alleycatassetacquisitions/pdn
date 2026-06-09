#pragma once

#include <gtest/gtest.h>
#include <algorithm>
#include "device/peer-graph.hpp"

class PeerGraphTests : public testing::Test {
public:
    net::Mac mac(uint8_t last) {
        return {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, last};
    }
};

inline void peerGraphStoresBeaconBySource(PeerGraphTests* suite) {
    PeerGraph g;
    g.setSelfMac(suite->mac(0x01));
    BeaconRecord b{suite->mac(0x02), suite->mac(0x01), {}};
    g.acceptBeacon(b, 0);
    auto cached = g.getBeacon(suite->mac(0x02));
    ASSERT_TRUE(cached.has_value());
    EXPECT_EQ((*cached).source, suite->mac(0x02));
}

inline void peerGraphMutualEdgeRequiresBothClaims(PeerGraphTests* suite) {
    PeerGraph g;
    g.setSelfMac(suite->mac(0x01));
    g.setSelfPeers(suite->mac(0x02), {}, 0);  // self claims 0x02 on IN
    // Without 0x02's beacon there is no mutual edge yet.
    EXPECT_EQ(g.getChainMembers().size(), 1u);
    // 0x02 claims self on its OUT. Now the edge is mutual.
    g.acceptBeacon({suite->mac(0x02), {}, suite->mac(0x01)}, 0);
    // Two devices mutually claiming each other is a single edge, not a cycle
    // of length >= 3 through self, so isInLoop stays false but both are members.
    EXPECT_FALSE(g.isInLoop());
    EXPECT_EQ(g.getChainMembers().size(), 2u);
}

inline void peerGraphIsInLoopThreeNodeRing(PeerGraphTests* suite) {
    // Ring: self(0x01) IN<-0x02, OUT->0x03; 0x02 IN<-0x03, OUT->0x01;
    //       0x03 IN<-0x01, OUT->0x02.
    PeerGraph g;
    g.setSelfMac(suite->mac(0x01));
    g.setSelfPeers(suite->mac(0x02), suite->mac(0x03), 0);
    g.acceptBeacon({suite->mac(0x02), suite->mac(0x03), suite->mac(0x01)}, 0);
    g.acceptBeacon({suite->mac(0x03), suite->mac(0x01), suite->mac(0x02)}, 0);
    EXPECT_TRUE(g.isInLoop());
    EXPECT_EQ(g.getChainMembers().size(), 3u);
}

inline void peerGraphThreeNodeChainNotInLoop(PeerGraphTests* suite) {
    // Chain: 0x02 <- self(0x01) -> 0x03. self is the middle; ends are open.
    PeerGraph g;
    g.setSelfMac(suite->mac(0x01));
    g.setSelfPeers(suite->mac(0x02), suite->mac(0x03), 0);
    g.acceptBeacon({suite->mac(0x02), {}, suite->mac(0x01)}, 0);
    g.acceptBeacon({suite->mac(0x03), suite->mac(0x01), {}}, 0);
    EXPECT_FALSE(g.isInLoop());
    EXPECT_EQ(g.getChainMembers().size(), 3u);
}

inline void peerGraphFourNodeRingInLoop(PeerGraphTests* suite) {
    PeerGraph g;
    g.setSelfMac(suite->mac(0x01));
    g.setSelfPeers(suite->mac(0x02), suite->mac(0x04), 0);
    g.acceptBeacon({suite->mac(0x02), suite->mac(0x03), suite->mac(0x01)}, 0);
    g.acceptBeacon({suite->mac(0x03), suite->mac(0x04), suite->mac(0x02)}, 0);
    g.acceptBeacon({suite->mac(0x04), suite->mac(0x01), suite->mac(0x03)}, 0);
    EXPECT_TRUE(g.isInLoop());
    EXPECT_EQ(g.getChainMembers().size(), 4u);
}

// An open jack reads as an all-zero peer. If acceptBeacon cached an all-zero
// (or broadcast) source as a node, hasMutualEdge(realNode, {0}) would hold for
// any device with a free jack, fabricating a false loop and inflating the
// chain. The poison gate must reject the beacon at the cache owner, before it
// ever enters beaconsBySource_.
inline void peerGraphRejectsPoisonBeaconSource(PeerGraphTests* suite) {
    PeerGraph g;
    g.setSelfMac(suite->mac(0x01));
    g.setSelfPeers({}, {}, 0);  // both jacks open -> self peers are all-zero

    // All-zero source: rejected, not cached, no graph-change side effect.
    EXPECT_FALSE(g.acceptBeacon({net::Mac{}, suite->mac(0x01), {}}, 100));
    EXPECT_FALSE(g.getBeacon(net::Mac{}).has_value());

    // Broadcast source: also rejected.
    net::Mac broadcast{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    EXPECT_FALSE(g.acceptBeacon({broadcast, suite->mac(0x01), {}}, 100));
    EXPECT_FALSE(g.getBeacon(broadcast).has_value());

    // No poison node leaked in: self with two open jacks is an island, never a loop.
    EXPECT_FALSE(g.isInLoop());
    EXPECT_EQ(g.getChainMembers().size(), 1u);

    // inPeer/outPeer may legitimately be all-zero (open jacks); only source is
    // validated, so a real source with open peers is still accepted.
    EXPECT_TRUE(g.acceptBeacon({suite->mac(0x02), {}, {}}, 200));
    EXPECT_TRUE(g.getBeacon(suite->mac(0x02)).has_value());
}

inline void peerGraphSelfIslandNotInLoop(PeerGraphTests* suite) {
    PeerGraph g;
    g.setSelfMac(suite->mac(0x01));
    EXPECT_FALSE(g.isInLoop());
    EXPECT_EQ(g.getChainMembers().size(), 1u);
}

inline void peerGraphTopologyStableAfterDebounceWindow(PeerGraphTests* suite) {
    PeerGraph g;
    g.setSelfMac(suite->mac(0x01));
    // self<->0x02 becomes a mutual edge at t=0: a real topology change.
    g.setSelfPeers(suite->mac(0x02), {}, 0);
    g.acceptBeacon({suite->mac(0x02), {}, suite->mac(0x01)}, 0);
    // 100ms after the change: still inside the 200ms window.
    EXPECT_FALSE(g.isTopologyStable(100));
    // 250ms after the change: past the window.
    EXPECT_TRUE(g.isTopologyStable(250));
    // A second mutual edge self<->0x03 forms at 300: resets the window.
    g.setSelfPeers(suite->mac(0x02), suite->mac(0x03), 300);
    g.acceptBeacon({suite->mac(0x03), suite->mac(0x01), {}}, 300);
    EXPECT_FALSE(g.isTopologyStable(400));
    EXPECT_TRUE(g.isTopologyStable(550));
}

// A backwards/zero clock must report unstable, not underflow unsigned
// subtraction into a near-UINT32_MAX gap that reads as falsely stable.
inline void peerGraphBackwardsClockNotStable(PeerGraphTests* suite) {
    PeerGraph g;
    g.setSelfMac(suite->mac(0x01));
    g.setSelfPeers(suite->mac(0x02), {}, 500);
    g.acceptBeacon({suite->mac(0x02), {}, suite->mac(0x01)}, 500);  // mutual edge at 500
    EXPECT_FALSE(g.isTopologyStable(0));
    EXPECT_FALSE(g.isTopologyStable(499));
    // Forward progress past the window still reads stable.
    EXPECT_TRUE(g.isTopologyStable(800));
}

inline void peerGraphUnchangedBeaconDoesNotResetStability(PeerGraphTests* suite) {
    PeerGraph g;
    g.setSelfMac(suite->mac(0x01));
    g.setSelfPeers(suite->mac(0x02), {}, 0);
    g.acceptBeacon({suite->mac(0x02), {}, suite->mac(0x01)}, 0);  // mutual edge at 0
    EXPECT_TRUE(g.isTopologyStable(250));
    // Re-receiving the identical beacon must NOT reset the stability window.
    bool changed = g.acceptBeacon({suite->mac(0x02), {}, suite->mac(0x01)}, 300);
    EXPECT_FALSE(changed);
    EXPECT_TRUE(g.isTopologyStable(310));
}

// A beacon whose content changed (so it re-floods) but whose mutual edges are
// unchanged must NOT reset the stability window: the window keys on topology,
// not beacon content. Here 0x02's role byte flips while its peers stay put.
inline void peerGraphRoleFlipDoesNotResetStability(PeerGraphTests* suite) {
    PeerGraph g;
    g.setSelfMac(suite->mac(0x01));
    g.setSelfPeers(suite->mac(0x02), {}, 0);
    g.acceptBeacon({suite->mac(0x02), {}, suite->mac(0x01), 0}, 0);  // mutual edge at 0
    EXPECT_TRUE(g.isTopologyStable(250));
    bool changed = g.acceptBeacon({suite->mac(0x02), {}, suite->mac(0x01), 1}, 300);
    EXPECT_TRUE(changed);                    // content differs, so it re-floods
    EXPECT_TRUE(g.isTopologyStable(310));    // but the edge set is identical
}

// A half-open arrival (a new node claims a peer that does not claim it back)
// adds no mutual edge, so it re-floods but does not reset the window.
inline void peerGraphHalfOpenBeaconDoesNotResetStability(PeerGraphTests* suite) {
    PeerGraph g;
    g.setSelfMac(suite->mac(0x01));
    g.setSelfPeers(suite->mac(0x02), {}, 0);
    g.acceptBeacon({suite->mac(0x02), {}, suite->mac(0x01)}, 0);  // mutual edge at 0
    EXPECT_TRUE(g.isTopologyStable(250));
    // 0x03 claims 0x02, but 0x02's cached beacon does not claim 0x03 back.
    bool changed = g.acceptBeacon({suite->mac(0x03), {}, suite->mac(0x02)}, 300);
    EXPECT_TRUE(changed);
    EXPECT_TRUE(g.isTopologyStable(310));
}

inline void peerGraphNonMutualNodeExcludedFromMembers(PeerGraphTests* suite) {
    PeerGraph g;
    g.setSelfMac(suite->mac(0x01));
    g.setSelfPeers(suite->mac(0x02), {}, 0);
    g.acceptBeacon({suite->mac(0x02), {}, suite->mac(0x01)}, 0);  // mutual with self
    // 0x03 claims 0x02 but no one claims 0x03 back: zero mutual edges. It is
    // retained in the cache (harmless) but excluded from the membership set.
    g.acceptBeacon({suite->mac(0x03), suite->mac(0x02), {}}, 0);
    auto members = g.getChainMembers();
    EXPECT_EQ(members.size(), 2u);
    EXPECT_EQ(std::find(members.begin(), members.end(), suite->mac(0x03)), members.end());
}

inline void peerGraphCountReachableSplitsChainAtSelf(PeerGraphTests* suite) {
    // Linear chain: 0x04 - 0x02 - self(0x01) - 0x03 - 0x05.
    // self IN<-0x02, OUT->0x03. Counting through the IN side (0x02) reaches
    // 0x02 and 0x04 (2); through the OUT side (0x03) reaches 0x03 and 0x05 (2).
    // self is never crossed, so the two sides never bleed into each other.
    PeerGraph g;
    g.setSelfMac(suite->mac(0x01));
    g.setSelfPeers(suite->mac(0x02), suite->mac(0x03), 0);
    g.acceptBeacon({suite->mac(0x02), suite->mac(0x04), suite->mac(0x01)}, 0);
    g.acceptBeacon({suite->mac(0x03), suite->mac(0x01), suite->mac(0x05)}, 0);
    g.acceptBeacon({suite->mac(0x04), {}, suite->mac(0x02)}, 0);
    g.acceptBeacon({suite->mac(0x05), suite->mac(0x03), {}}, 0);
    EXPECT_EQ(g.countReachableExcludingSelf(suite->mac(0x02)), 2u);
    EXPECT_EQ(g.countReachableExcludingSelf(suite->mac(0x03)), 2u);
}

inline void peerGraphCountReachableDirectPeerBeforeBeacon(PeerGraphTests* suite) {
    // A freshly connected direct peer counts as 1 even before its beacon
    // arrives; it is a confirmed physical neighbor. Once its beacon lands
    // showing another hop behind it, the count grows.
    PeerGraph g;
    g.setSelfMac(suite->mac(0x01));
    g.setSelfPeers(suite->mac(0x02), {}, 0);
    EXPECT_EQ(g.countReachableExcludingSelf(suite->mac(0x02)), 1u);
    g.acceptBeacon({suite->mac(0x02), suite->mac(0x04), suite->mac(0x01)}, 0);
    g.acceptBeacon({suite->mac(0x04), {}, suite->mac(0x02)}, 0);
    EXPECT_EQ(g.countReachableExcludingSelf(suite->mac(0x02)), 2u);
}

inline void peerGraphCountReachableZeroForAbsentPeer(PeerGraphTests* suite) {
    PeerGraph g;
    g.setSelfMac(suite->mac(0x01));
    EXPECT_EQ(g.countReachableExcludingSelf({}), 0u);
    EXPECT_EQ(g.countReachableExcludingSelf(suite->mac(0x01)), 0u);  // self
}

inline void peerGraphTwoDeviceBothJacksNotLoopButRetainsPeer(PeerGraphTests* suite) {
    // Two devices wired with BOTH cables: self has the same peer on IN and OUT.
    // Only two distinct nodes exist, so this is not a >=3-node cycle (isInLoop
    // stays false), but the peer is a member and stays one when a single jack is
    // yanked, because the other jack still claims it. declareJackDead relies on
    // exactly this to keep the ESP-NOW peer slot while either cable is live.
    PeerGraph g;
    g.setSelfMac(suite->mac(0x01));
    g.setSelfPeers(suite->mac(0x02), suite->mac(0x02), 0);  // same peer both jacks
    g.acceptBeacon({suite->mac(0x02), suite->mac(0x01), suite->mac(0x01)}, 0);
    EXPECT_FALSE(g.isInLoop());
    EXPECT_EQ(g.getChainMembers().size(), 2u);
    EXPECT_EQ(g.countReachableExcludingSelf(suite->mac(0x02)), 1u);

    // Yank the OUT jack: self still claims 0x02 on IN, edge stays mutual.
    g.setSelfPeers(suite->mac(0x02), {}, 100);
    EXPECT_EQ(g.getChainMembers().size(), 2u);

    // Yank the IN jack too: no claim remains, peer drops out.
    g.setSelfPeers({}, {}, 200);
    EXPECT_EQ(g.getChainMembers().size(), 1u);
    EXPECT_FALSE(g.isInLoop());
}

inline void peerGraphPeerDropsOutWhenSelfStopsClaiming(PeerGraphTests* suite) {
    // A device leaves the ring: self's macPeer clears, self stops claiming it,
    // so the edge is no longer mutual and the peer leaves the membership set.
    PeerGraph g;
    g.setSelfMac(suite->mac(0x01));
    g.setSelfPeers(suite->mac(0x02), {}, 0);
    g.acceptBeacon({suite->mac(0x02), {}, suite->mac(0x01)}, 0);
    EXPECT_EQ(g.getChainMembers().size(), 2u);
    // Self drops the peer (cable yank). The 0x02 edge is no longer mutual.
    g.setSelfPeers({}, {}, 100);
    EXPECT_EQ(g.getChainMembers().size(), 1u);
    EXPECT_FALSE(g.isInLoop());
}
