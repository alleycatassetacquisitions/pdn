#include "device/peer-graph.hpp"

#include <algorithm>

void PeerGraph::setSelfMac(const net::Mac& mac) {
    selfMac_ = mac;
}

bool PeerGraph::acceptBeacon(const BeaconRecord& beacon, unsigned long nowMs) {
    // Reject poison at the layer that owns the cache. An all-zero/broadcast
    // source cached as a node corrupts loop detection: an open jack is itself an
    // all-zero peer, so hasMutualEdge(real, {0}) holds for any node with a free
    // jack, fabricating a false loop and inflating the chain count. inPeer/outPeer
    // may legitimately be all-zero (an open jack), so only source is validated.
    if (!net::isValidPeerMac(beacon.source)) return false;
    // Self is the authority on its own peers (setSelfPeers); a self-beacon that
    // floods back around a ring carries no new information and would otherwise
    // cost an extra ring trip and reset the stability window.
    if (beacon.source == selfMac_) return false;

    auto it = beaconsBySource_.find(beacon.source);
    if (it != beaconsBySource_.end() && it->second == beacon) return false;
    beaconsBySource_[beacon.source] = beacon;
    lastGraphChangeMs_ = nowMs;
    if (onGraphChanged_) onGraphChanged_();
    return true;
}

std::optional<BeaconRecord> PeerGraph::getBeacon(const net::Mac& source) const {
    auto it = beaconsBySource_.find(source);
    if (it == beaconsBySource_.end()) return std::nullopt;
    return it->second;
}

void PeerGraph::setSelfPeers(const net::Mac& inPeer,
                             const net::Mac& outPeer,
                             unsigned long nowMs) {
    if (selfInPeer_ == inPeer && selfOutPeer_ == outPeer) return;
    selfInPeer_ = inPeer;
    selfOutPeer_ = outPeer;
    lastGraphChangeMs_ = nowMs;
    if (onGraphChanged_) onGraphChanged_();
}

bool PeerGraph::hasMutualEdge(const net::Mac& a, const net::Mac& b) const {
    auto claims = [this](const net::Mac& from, const net::Mac& to) -> bool {
        if (from == selfMac_) {
            return selfInPeer_ == to || selfOutPeer_ == to;
        }
        auto it = beaconsBySource_.find(from);
        if (it == beaconsBySource_.end()) return false;
        return it->second.inPeer == to || it->second.outPeer == to;
    };
    return claims(a, b) && claims(b, a);
}

std::vector<net::Mac> PeerGraph::reachableFrom(
    const net::Mac& start, std::initializer_list<net::Mac> blocked) const {
    std::vector<net::Mac> visited(blocked);
    std::vector<net::Mac> result;
    std::vector<net::Mac> frontier;
    // start is reachable from itself, unless it is itself blocked.
    if (std::find(visited.begin(), visited.end(), start) == visited.end()) {
        visited.push_back(start);
        result.push_back(start);
        frontier.push_back(start);
    }
    while (!frontier.empty()) {
        net::Mac current = frontier.back();
        frontier.pop_back();
        for (const auto& entry : beaconsBySource_) {
            const net::Mac& source = entry.first;
            if (std::find(visited.begin(), visited.end(), source) != visited.end()) continue;
            if (hasMutualEdge(current, source)) {
                visited.push_back(source);
                result.push_back(source);
                frontier.push_back(source);
            }
        }
    }
    return result;
}

std::vector<net::Mac> PeerGraph::getChainMembers() const {
    // Everything reachable from self over mutual edges (self included).
    return reachableFrom(selfMac_, {});
}

size_t PeerGraph::countReachableExcludingSelf(const net::Mac& firstHop) const {
    if (firstHop == net::Mac{} || firstHop == selfMac_) return 0;
    // firstHop must be a peer self actually claims on a jack. Counting it as a
    // confirmed direct peer before its beacon arrives is only sound because self
    // vouches for the physical link; a MAC self does not claim (e.g. a stale
    // value held after a yank) is not a member and must contribute nothing.
    if (selfInPeer_ != firstHop && selfOutPeer_ != firstHop) return 0;
    // Block self so traversal never crosses to the far side of the chain;
    // expansion past firstHop follows mutual edges.
    return reachableFrom(firstHop, {selfMac_}).size();
}

bool PeerGraph::isInLoop() const {
    // Self is in a cycle iff two distinct mutual neighbors of self are
    // connected to each other by a path that does not pass back through self.
    std::vector<net::Mac> neighbors;
    for (const auto& entry : beaconsBySource_) {
        if (hasMutualEdge(selfMac_, entry.first)) neighbors.push_back(entry.first);
    }
    if (neighbors.size() < 2) return false;

    for (size_t i = 0; i < neighbors.size(); ++i) {
        // Reachability from neighbors[i] avoiding self is the same set for every
        // partner j, so compute it once per i.
        std::vector<net::Mac> reach = reachableFrom(neighbors[i], {selfMac_});
        for (size_t j = i + 1; j < neighbors.size(); ++j) {
            if (std::find(reach.begin(), reach.end(), neighbors[j]) != reach.end()) return true;
        }
    }
    return false;
}

bool PeerGraph::isTopologyStable(unsigned long nowMs) const {
    // No recorded change yet means no settled topology to report; a just-booted
    // device must read as unstable, not stable-since-time-zero, so a premature
    // coordinator can't claim through.
    if (!lastGraphChangeMs_) return false;
    // Clamp the gap to 0 on a backwards/zero clock so unsigned subtraction can't
    // underflow to ~UINT32_MAX and falsely report stability.
    const unsigned long last = *lastGraphChangeMs_;
    const unsigned long gap = nowMs >= last ? nowMs - last : 0;
    return gap >= kTopologyStabilityMs;
}

