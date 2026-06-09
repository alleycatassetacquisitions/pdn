//
// Created by Elli Furedy on 10/11/2024.
//

#include <gtest/gtest.h>

#include "device-mock.hpp"
#include "device/drivers/native/native-serial-driver.hpp"

#include <cstdint>
#include <string>
#include <vector>

using namespace std;

// ===== Byte-level RX/TX API tests =====

inline void nativeSerialDriverByteCallbackReceivesArbitraryBytes() {
    NativeSerialDriver drv("test");
    drv.initialize();
    std::vector<uint8_t> received;
    drv.setBytesCallback([&](const uint8_t* data, size_t len) {
        for (size_t i = 0; i < len; ++i) received.push_back(data[i]);
    });
    uint8_t payload[] = {0xAA, 0x55, 0x01, 0x02, 0x03};
    drv.injectInputBytes(payload, sizeof(payload));
    drv.exec();
    ASSERT_EQ(received.size(), 5u);
    EXPECT_EQ(received[0], 0xAA);
    EXPECT_EQ(received[4], 0x03);
}

inline void nativeSerialDriverByteCallbackFragmentsWhenEnabled() {
    NativeSerialDriver drv("test");
    drv.initialize();
    drv.setFragmentDeliveriesForTest(true);
    int callbackCount = 0;
    size_t totalBytes = 0;
    size_t maxChunk = 0;
    drv.setBytesCallback([&](const uint8_t* /*data*/, size_t len) {
        callbackCount++;
        totalBytes += len;
        if (len > maxChunk) maxChunk = len;
    });
    uint8_t payload[10] = {1,2,3,4,5,6,7,8,9,10};
    drv.injectInputBytes(payload, sizeof(payload));
    for (int i = 0; i < 20; ++i) drv.exec();
    EXPECT_EQ(totalBytes, 10u);
    EXPECT_LE(maxChunk, 3u);       // no chunk exceeds the deterministic 1-2-3 cycle
    EXPECT_GE(callbackCount, 4);   // 10 bytes / 3-max per call = at least 4 calls
}
