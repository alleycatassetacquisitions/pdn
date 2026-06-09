#include "device/peer-graph.hpp"

#include <algorithm>

void PeerGraph::setSelfMac(const net::Mac& mac) {
    selfMac_ = mac;
    invalidateDerived();
}

void PeerGraph::setSelfRole(uint8_t role) {
    selfRole_ = role;
}

bool PeerGraph::acceptBeacon(const BeaconRecord& beacon, unsigned long nowMs) {
    // Reject poison at the layer that owns the cache. An all-zero/broadcast
    // source cached as a node corrupts loop detection: an open jack is itself an
    // all-zero peer, so hasMutualEdge(real, {0}) holds for any node with a free
    // jack, fabricating a false loop and inflating the chain count. inPeer/outPeer
    // may legitimately be all-zero (an open jack), so only source is validated.
    if (!net::isValidPeerMac(beacon.source)) return false;
    auto it = beaconsBySource_.find(beacon.source);
    if (it != beaconsBySource_.end() && it->second == beacon) {
        return false;
    }
    beaconsBySource_[beacon.source] = beacon;
    invalidateDerived();
    restampOnEdgeChange(nowMs);
    return true;
}

std::optional<BeaconRecord> PeerGraph::getBeacon(const net::Mac& source) const {
    auto it = beaconsBySource_.find(source);
    if (it == beaconsBySource_.end()) return std::nullopt;
    return it->second;
}

std::vector<BeaconRecord> PeerGraph::allBeacons() const {
    std::vector<BeaconRecord> out;
    out.reserve(beaconsBySource_.size());
    for (const auto& entry : beaconsBySource_) out.push_back(entry.second);
    return out;
}

void PeerGraph::setSelfPeers(const net::Mac& inPeer,
                             const net::Mac& outPeer,
                             unsigned long nowMs) {
    if (selfInPeer_ == inPeer && selfOutPeer_ == outPeer) return;
    selfInPeer_ = inPeer;
    selfOutPeer_ = outPeer;
    invalidateDerived();
    restampOnEdgeChange(nowMs);
}

std::vector<std::pair<net::Mac, net::Mac>> PeerGraph::currentMutualEdges() const {
    // Self is never cached (it lives in the self-* slots), but skip it
    // defensively so a node that hears its own beacon echoed back can't
    // appear twice.
    std::vector<net::Mac> nodes;
    nodes.push_back(selfMac_);
    for (const auto& entry : beaconsBySource_) {
        if (entry.first != selfMac_) nodes.push_back(entry.first);
    }
    std::vector<std::pair<net::Mac, net::Mac>> edges;
    for (size_t i = 0; i < nodes.size(); ++i) {
        for (size_t j = i + 1; j < nodes.size(); ++j) {
            if (hasMutualEdge(nodes[i], nodes[j])) {
                edges.emplace_back(std::min(nodes[i], nodes[j]),
                                   std::max(nodes[i], nodes[j]));
            }
        }
    }
    std::sort(edges.begin(), edges.end());
    return edges;
}

void PeerGraph::restampOnEdgeChange(unsigned long nowMs) {
    auto edges = currentMutualEdges();
    if (edges == lastMutualEdges_) return;
    lastMutualEdges_ = std::move(edges);
    lastGraphChangeMs_ = nowMs;
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
    if (!cachedChainMembers_.has_value()) {
        cachedChainMembers_ = reachableFrom(selfMac_, {});
    }
    return *cachedChainMembers_;
}

size_t PeerGraph::countReachableExcludingSelf(const net::Mac& firstHop) const {
    if (firstHop == net::Mac{} || firstHop == selfMac_) return 0;
    // Block self so traversal never crosses to the far side of the chain.
    return reachableFrom(firstHop, {selfMac_}).size();
}

bool PeerGraph::isInLoop() const {
    if (cachedIsInLoop_.has_value()) return *cachedIsInLoop_;
    // Self is in a cycle iff two distinct mutual neighbors of self are
    // connected to each other by a path that does not pass back through self.
    cachedIsInLoop_ = false;
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
            if (std::find(reach.begin(), reach.end(), neighbors[j]) != reach.end()) {
                cachedIsInLoop_ = true;
                return true;
            }
        }
    }
    return false;
}

std::optional<PeerGraph::NodeView> PeerGraph::nodeView(const net::Mac& node) const {
    if (node == selfMac_) {
        return NodeView{selfRole_, selfInPeer_, selfOutPeer_};
    }
    auto it = beaconsBySource_.find(node);
    if (it == beaconsBySource_.end()) return std::nullopt;
    return NodeView{it->second.role, it->second.inPeer, it->second.outPeer};
}

bool PeerGraph::isTopologyStable(unsigned long nowMs) const {
    // Clamp the gap to 0 on a backwards/zero clock so unsigned subtraction can't
    // underflow to ~UINT32_MAX and falsely report stability (which would let a
    // premature coordinator claim through). Mirrors the silent-link gap guard in
    // RemoteDeviceCoordinator.
    const unsigned long gap = nowMs >= lastGraphChangeMs_ ? nowMs - lastGraphChangeMs_ : 0;
    return gap >= kTopologyStabilityMs;
}

