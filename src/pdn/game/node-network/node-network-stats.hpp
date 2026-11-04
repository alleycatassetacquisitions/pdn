#pragma once

#include <cstdint>

#include "device/drivers/storage-interface.hpp"

/** NVS namespace for NodeNetwork counters and seen peer MACs. */
inline constexpr const char NODE_NETWORK_STATS_PREFS_NAMESPACE[] = "nodenet";

/** Persists Nodes Created and MACs that already counted toward it. */
class NodeNetworkStats {
public:
    static constexpr size_t MAX_SEEN_PEERS = 32;

    explicit NodeNetworkStats(StorageInterface* storage);

    uint32_t getNodesCreated() const;

    /**
     * After a successful upload with @p peerMac, increments the counter when that MAC is new.
     *
     * @return true when the count was incremented.
     */
    bool recordSuccessfulUploadPartner(const uint8_t peerMac[6]);

private:
    bool hasSeenPeer(const uint8_t peerMac[6]) const;
    void markPeerSeen(const uint8_t peerMac[6]);
    void persistCount();

    StorageInterface* storage = nullptr;
    uint32_t nodesCreated = 0;
};
