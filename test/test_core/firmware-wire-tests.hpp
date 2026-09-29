#pragma once

#include "device/drivers/peer-comms-types.hpp"
#include <gtest/gtest.h>

TEST(FirmwareWireTest, everyFrameFitsOneV2Frame) {
  EXPECT_LE(sizeof(FirmwareOfferPayload), MAX_PKT_DATA_SIZE);
  EXPECT_LE(sizeof(FirmwareStatusPayload), MAX_PKT_DATA_SIZE);
  EXPECT_LE(sizeof(FirmwareCompletePayload), MAX_PKT_DATA_SIZE);
}

TEST(FirmwareWireTest, bitmapCoversTheDeclaredChunkCeiling) {
  EXPECT_EQ(FIRMWARE_BITMAP_BYTES * 8, FIRMWARE_MAX_CHUNKS);
}

TEST(FirmwareWireTest, chunkPayloadFillsTheFrame) {
  EXPECT_GE(MAX_PKT_DATA_SIZE - sizeof(FirmwareChunkHeader), 1322u);
}
