#pragma once

#include <gtest/gtest.h>
#include "device/drivers/peer-comms-types.hpp"

TEST(PeerCommsTypesTest, headerIsOneByte) {
    EXPECT_EQ(sizeof(DataPktHdr), 1u);
}
