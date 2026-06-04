#pragma once

#include "device/mac-types.hpp"  // net::Mac
#include <functional>
#include <map>
#include <optional>
#include <vector>

// One device's direct-peer view, as carried in a BEACON frame. An all-zero
// peer MAC means "no peer on that jack". Equality is by full content, so the
// flood layer can dedup a re-received beacon against the cached one; freshness
// after a transient is restored by the 1Hz BEACON backstop, not a seq number.
struct BeaconRecord {
    net::Mac source{};
    net::Mac inPeer{};
    net::Mac outPeer{};

    bool operator==(const BeaconRecord& other) const {
        return source == other.source && inPeer == other.inPeer && outPeer == other.outPeer;
    }
    bool operator!=(const BeaconRecord& other) const { return !(*this == other); }
};

// Derives ring topology from a cache of per-source beacons using the
// mutual-consistency rule: an edge (A <-> B) exists only when both A's and B's
// latest beacons claim each other. Loop detection and membership are computed
// on demand from that cache; there is no separately-maintained roster.
class PeerGraph {
public:
    void setSelfMac(const net::Mac& mac);
    const net::Mac& getSelfMac() const { return selfMac_; }

    // Cache a received beacon. Returns true if the cache changed (caller floods
    // the beacon onward when so). A beacon identical to the cached one for its
    // source returns false — that is the flood-dedup signal. A self-sourced
    // beacon (source == selfMac_) is always dropped: self is the authority on its
    // own peers via setSelfPeers, so re-accepting one flooded back around a ring
    // would only cost an extra ring trip and reset the stability window.
    bool acceptBeacon(const BeaconRecord& beacon, unsigned long nowMs);

    std::optional<BeaconRecord> getBeacon(const net::Mac& source) const;

    // Update this device's own peer slots (from macPeer state). Triggers a
    // graph-change notification when the slots actually change.
    void setSelfPeers(const net::Mac& inPeer,
                      const net::Mac& outPeer,
                      unsigned long nowMs);

    bool isInLoop() const;
    std::vector<net::Mac> getChainMembers() const;

    // Count of members reachable starting through `firstHop`, never crossing
    // back through self. In a linear chain, removing self splits it into two
    // sides; passing the peer on one jack counts only that side. firstHop
    // itself counts as 1 even before its beacon arrives (it is a confirmed
    // direct peer); further hops require mutual edges. Returns 0 if firstHop is
    // absent, is self, or is not a MAC self currently claims on a jack — the
    // count must not vouch for a node self has no live link to.
    size_t countReachableExcludingSelf(const net::Mac& firstHop) const;

    bool isTopologyStable(unsigned long nowMs) const;

    using GraphChangedCallback = std::function<void()>;
    void setOnGraphChanged(GraphChangedCallback cb) { onGraphChanged_ = std::move(cb); }

private:
    net::Mac selfMac_{};
    net::Mac selfInPeer_{};
    net::Mac selfOutPeer_{};
    std::map<net::Mac, BeaconRecord> beaconsBySource_;
    // Unset until the first change is recorded. A graph that has never changed
    // has no settled topology to report, so it reads as unstable rather than
    // stable-since-time-zero — the absence of a timestamp says so directly.
    std::optional<unsigned long> lastGraphChangeMs_;
    GraphChangedCallback onGraphChanged_;

    static constexpr unsigned long kTopologyStabilityMs = 200;

    bool hasMutualEdge(const net::Mac& a, const net::Mac& b) const;

    // Nodes reachable from `start` by walking mutual edges, never crossing any
    // node in `blocked` and never revisiting. `start` is always included in the
    // result. The membership/count/loop queries differ only in their seed and
    // what they tally, so they all express their walk through this.
    std::vector<net::Mac> reachableFrom(
        const net::Mac& start, std::initializer_list<net::Mac> blocked) const;
};
