#pragma once

#include <gtest/gtest.h>

#include <array>
#include <cstring>

#include "game/node-network/node-network-stats.hpp"
#include "match-storage-tests.hpp"

inline void nodeNetworkStatsStartsAtZero() {
    InMemoryStorage backing;
    NodeNetworkStats stats(&backing);
    EXPECT_EQ(stats.getNodesCreated(), 0u);
}

inline void nodeNetworkStatsIncrementsOncePerNewMac() {
    InMemoryStorage backing;
    NodeNetworkStats stats(&backing);
    const uint8_t macA[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x0A};
    const uint8_t macB[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x0B};

    EXPECT_TRUE(stats.recordSuccessfulUploadPartner(macA));
    EXPECT_EQ(stats.getNodesCreated(), 1u);
    EXPECT_FALSE(stats.recordSuccessfulUploadPartner(macA));
    EXPECT_EQ(stats.getNodesCreated(), 1u);
    EXPECT_TRUE(stats.recordSuccessfulUploadPartner(macB));
    EXPECT_EQ(stats.getNodesCreated(), 2u);
}

inline void nodeNetworkStatsReloadsCountFromStorage() {
    InMemoryStorage backing;
    const uint8_t mac[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    {
        NodeNetworkStats stats(&backing);
        EXPECT_TRUE(stats.recordSuccessfulUploadPartner(mac));
    }
    NodeNetworkStats reloaded(&backing);
    EXPECT_EQ(reloaded.getNodesCreated(), 1u);
    EXPECT_FALSE(reloaded.recordSuccessfulUploadPartner(mac));
}
