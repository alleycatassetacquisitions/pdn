#pragma once

#include "device/mac-types.hpp"  // net::Mac
#include <map>
#include <optional>
#include <utility>
#include <vector>

// One device's direct-peer view, as carried in a BEACON frame. An all-zero
// peer MAC means "no peer on that jack". Equality is by full content so the
// flood layer can dedup re-received beacons without sequence numbers.
struct BeaconRecord {
    net::Mac source{};
    net::Mac inPeer{};
    net::Mac outPeer{};
    // Opaque game-layer byte (encoding: chain_role::Role); this layer floods
    // and dedups it, never interprets it. Two hardware-tested invariants: it
    // stays in operator== (a change must re-flood), and it rides the topology
    // packet; a separate role flood diverged (all-supporters limbo).
    uint8_t role = 0;

    bool operator==(const BeaconRecord& other) const {
        return source == other.source && inPeer == other.inPeer &&
               outPeer == other.outPeer && role == other.role;
    }
    bool operator!=(const BeaconRecord& other) const { return !(*this == other); }
};

// Derives ring topology from a cache of per-source beacons using the
// mutual-consistency rule: an edge (A <-> B) exists only when both A's and B's
// latest beacons claim each other. Loop detection and membership are computed
// on demand from that cache; there is no separately-maintained roster. Derive,
// don't maintain: event-mutated rosters diverge permanently when one cascade
// message is lost, while a derived view self-corrects on the next beacon, and
// a stale or half-open claim merely fails mutuality instead of poisoning state.
class PeerGraph {
public:
    void setSelfMac(const net::Mac& mac);
    const net::Mac& getSelfMac() const { return selfMac_; }

    // Self-node counterpart of BeaconRecord::role, so nodeView() serves every
    // node alike. Not interpreted here.
    void setSelfRole(uint8_t role);

    // Cache a received beacon. Returns true if the cache changed (caller floods
    // the beacon onward when so). A beacon identical to the cached one for its
    // source returns false; that is the flood-dedup signal.
    bool acceptBeacon(const BeaconRecord& beacon, unsigned long nowMs);

    std::optional<BeaconRecord> getBeacon(const net::Mac& source) const;

    // Every cached per-source beacon (self excluded). Replayed to a freshly
    // connected neighbor so a late joiner/reboot relearns nodes whose unchanged
    // beacons the change-gated flood never re-delivers.
    std::vector<BeaconRecord> allBeacons() const;

    // Update this device's own peer slots. Restamps the stability clock only on a
    // mutual-edge add/remove; a self-claim the peer hasn't reciprocated isn't yet
    // an edge, so it waits for the peer's beacon to complete (or break) it.
    void setSelfPeers(const net::Mac& inPeer,
                      const net::Mac& outPeer,
                      unsigned long nowMs);

    bool isInLoop() const;
    std::vector<net::Mac> getChainMembers() const;

    // Neutral per-node read for game-layer derivations (the champion walk):
    // self from the self-* slots, others from their cached beacon, so callers
    // treat every node alike. nullopt if the node is neither self nor cached.
    struct NodeView {
        uint8_t role = 0;
        net::Mac inPeer{};
        net::Mac outPeer{};
    };
    std::optional<NodeView> nodeView(const net::Mac& node) const;

    // True iff both nodes' latest claims (self slots or cached beacon) name
    // each other. The edge primitive every derivation here is built on; public
    // so game-layer walks traverse exactly the edges isInLoop sees.
    bool hasMutualEdge(const net::Mac& a, const net::Mac& b) const;

    // Members reachable through `firstHop` without crossing back through self
    // (so on a linear chain it counts only that side). firstHop counts as 1 even
    // before its beacon arrives (confirmed direct peer); further hops need mutual
    // edges. 0 if firstHop is absent or is self.
    size_t countReachableExcludingSelf(const net::Mac& firstHop) const;

    bool isTopologyStable(unsigned long nowMs) const;

private:
    net::Mac selfMac_{};
    net::Mac selfInPeer_{};
    net::Mac selfOutPeer_{};
    uint8_t selfRole_ = 0;
    std::map<net::Mac, BeaconRecord> beaconsBySource_;
    unsigned long lastGraphChangeMs_ = 0;
    // Mutual-edge set at the last restamp, normalized low-MAC-first and sorted so
    // equality is a plain vector compare. The stability clock advances only when
    // this set changes, so churn that leaves topology intact doesn't thrash it.
    std::vector<std::pair<net::Mac, net::Mac>> lastMutualEdges_;

    static constexpr unsigned long kTopologyStabilityMs = 200;

    // Current mutual-edge set, and the gate that restamps the stability clock
    // after any self-slot/cache mutation only when that set actually changed.
    std::vector<std::pair<net::Mac, net::Mac>> currentMutualEdges() const;
    void restampOnEdgeChange(unsigned long nowMs);

    // isInLoop()/getChainMembers() are O(V²) walks queried several times per
    // main-loop tick by transition predicates, while the graph mutates only on
    // cable/beacon events, so memoize and invalidate on mutation. mutable:
    // caches of const queries. Single-threaded (main loop) by design.
    void invalidateDerived() {
        cachedIsInLoop_.reset();
        cachedChainMembers_.reset();
    }
    mutable std::optional<bool> cachedIsInLoop_;
    mutable std::optional<std::vector<net::Mac>> cachedChainMembers_;

    // Nodes reachable from `start` over mutual edges, skipping `blocked`, no
    // revisits, `start` included. Membership/count/loop differ only in seed/tally.
    std::vector<net::Mac> reachableFrom(
        const net::Mac& start, std::initializer_list<net::Mac> blocked) const;
};
