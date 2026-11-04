#include "game/node-network/node-network-stats.hpp"

#include <cstring>
#include <string>

namespace {
const char* const NODES_CREATED_KEY = "nodes_created";
const char* const SEEN_COUNT_KEY = "seen_count";
const char* const SEEN_MAC_PREFIX = "seen_";

std::string seenKey(size_t index) {
    return SEEN_MAC_PREFIX + std::to_string(index);
}

uint32_t decodeUint32(const std::string& encoded) {
    if (encoded.size() != 4) {
        return 0;
    }
    uint32_t value = 0;
    for (size_t shift = 0; shift < 32; shift += 8) {
        value |= static_cast<uint32_t>(static_cast<uint8_t>(encoded[shift / 8])) << shift;
    }
    return value;
}

std::string encodeUint32(uint32_t value) {
    std::string out;
    for (size_t shift = 0; shift < 32; shift += 8) {
        out.push_back(static_cast<char>((value >> shift) & 0xFF));
    }
    return out;
}
}  // namespace

NodeNetworkStats::NodeNetworkStats(StorageInterface* storage) : storage(storage) {
    nodesCreated =
        decodeUint32(storage->read(NODE_NETWORK_STATS_PREFS_NAMESPACE, NODES_CREATED_KEY, ""));
}

uint32_t NodeNetworkStats::getNodesCreated() const {
    return nodesCreated;
}

bool NodeNetworkStats::hasSeenPeer(const uint8_t peerMac[6]) const {
    const size_t seenCount = storage->readUChar(NODE_NETWORK_STATS_PREFS_NAMESPACE, SEEN_COUNT_KEY, 0);
    for (size_t index = 0; index < seenCount; index++) {
        const std::string encoded = storage->read(NODE_NETWORK_STATS_PREFS_NAMESPACE, seenKey(index), "");
        if (encoded.size() == 6 && memcmp(encoded.data(), peerMac, 6) == 0) {
            return true;
        }
    }
    return false;
}

void NodeNetworkStats::markPeerSeen(const uint8_t peerMac[6]) {
    size_t seenCount = storage->readUChar(NODE_NETWORK_STATS_PREFS_NAMESPACE, SEEN_COUNT_KEY, 0);
    if (seenCount >= MAX_SEEN_PEERS) {
        return;
    }
    const std::string encoded(reinterpret_cast<const char*>(peerMac), 6);
    storage->write(NODE_NETWORK_STATS_PREFS_NAMESPACE, seenKey(seenCount), encoded);
    storage->writeUChar(NODE_NETWORK_STATS_PREFS_NAMESPACE, SEEN_COUNT_KEY,
                        static_cast<uint8_t>(seenCount + 1));
}

void NodeNetworkStats::persistCount() {
    storage->write(NODE_NETWORK_STATS_PREFS_NAMESPACE, NODES_CREATED_KEY, encodeUint32(nodesCreated));
}

bool NodeNetworkStats::recordSuccessfulUploadPartner(const uint8_t peerMac[6]) {
    if (hasSeenPeer(peerMac)) {
        return false;
    }
    markPeerSeen(peerMac);
    nodesCreated++;
    persistCount();
    return true;
}
