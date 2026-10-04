#pragma once

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "device-mock.hpp"

TEST(SendStatusTest, handlerReceivesTheFrameItWasRegisteredFor) {
    NiceMock<MockPeerComms> comms;
    static int calls = 0;
    static bool lastSuccess = false;
    calls = 0;

    comms.setSendStatusHandler(
        PktType::kFirmwareUpdate,
        [](const uint8_t*, const uint8_t*, size_t, bool success, void*) {
            calls++;
            lastSuccess = success;
        },
        nullptr);

    comms.fireSendStatus(PktType::kFirmwareUpdate, true);

    EXPECT_EQ(calls, 1);
    EXPECT_TRUE(lastSuccess);
}
