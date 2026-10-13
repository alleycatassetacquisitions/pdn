#pragma once

#include <gtest/gtest.h>
#include "device/drivers/peer-comms-types.hpp"

TEST(PeerCommsTypesTest, headerIsThreeBytesAndPayloadIsV2Sized) {
    EXPECT_EQ(sizeof(DataPktHdr), 3u);
    EXPECT_EQ(MAX_PKT_DATA_SIZE, 1470u - 3u);
}
