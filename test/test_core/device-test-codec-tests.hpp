#pragma once

#include <gtest/gtest.h>
#include <cstring>

#include "alleycat-server/device-test-codec.hpp"
#include "alleycat-server/device_api.pb.h"
#include <pb_decode.h>

inline bool decodeDeviceTestPayload(const std::string& payload, alleycat_device_WriteDeviceTestResultRequest& out) {
    out = alleycat_device_WriteDeviceTestResultRequest_init_zero;
    pb_istream_t stream = pb_istream_from_buffer(reinterpret_cast<const pb_byte_t*>(payload.data()), payload.size());
    return pb_decode(&stream, alleycat_device_WriteDeviceTestResultRequest_fields, &out);
}

inline void deviceTestCodecRoundTripEncodesFields() {
    DeviceTestResult result;
    result.hardwareVersion = 2;
    strncpy(result.firmwareCommitHash, "26a3bc12", sizeof(result.firmwareCommitHash));
    result.idLabel = 214;
    strncpy(result.macAddress, "AA:BB:CC:DD:EE:FF", sizeof(result.macAddress));
    result.components.screen = true;
    result.components.leds = true;
    result.components.primaryButton = true;
    result.components.secondaryButton = false;
    result.components.haptics = true;
    result.components.powerSwitch = true;
    result.components.inputJack = true;
    result.components.outputJack = true;
    result.components.housing = true;

    std::string payload;
    ASSERT_TRUE(DeviceTestCodec::encodeRequest(result, payload));
    ASSERT_FALSE(payload.empty());

    alleycat_device_WriteDeviceTestResultRequest message;
    ASSERT_TRUE(decodeDeviceTestPayload(payload, message));
    EXPECT_EQ(message.hardware_version, 2u);
    EXPECT_EQ(message.id_label, 214u);
    EXPECT_STREQ(message.firmware_commit_hash, "26a3bc12");
    EXPECT_STREQ(message.device_mac, "AA:BB:CC:DD:EE:FF");
    EXPECT_FALSE(message.has_tester_mac);
    EXPECT_TRUE(message.components.screen);
    EXPECT_FALSE(message.components.secondary_button);
}
