#pragma once

#include <gtest/gtest.h>
#include <cstring>
#include <pb_decode.h>

#include "alleycat-server/crash-encoder.hpp"
#include "alleycat-server/device_api.pb.h"
#include "device/crash/crash-record.hpp"

inline CrashRecord makeFullRecord() {
    CrashRecord rec{};
    rec.crashNumber    = 3;
    rec.timestamp      = 12500;
    rec.resetReason    = 7;
    rec.programCounter = 0x400D1A2C;
    rec.exceptionCause = 28;
    strncpy(rec.taskName,   "loopTask",  TASK_NAME_LENGTH  - 1);
    strncpy(rec.commitHash, "a1b2c3d4",  COMMIT_HASH_LENGTH - 1);
    return rec;
}

inline alleycat_device_WriteDeviceLogRequest decodeBytes(const uint8_t* buf, size_t len) {
    alleycat_device_WriteDeviceLogRequest msg =
        alleycat_device_WriteDeviceLogRequest_init_zero;
    pb_istream_t stream = pb_istream_from_buffer(buf, len);
    pb_decode(&stream, alleycat_device_WriteDeviceLogRequest_fields, &msg);
    return msg;
}

class CrashEncoderTestSuite : public testing::Test {
public:
    const uint8_t mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    uint8_t buf[CrashEncoder::MAX_ENCODED_SIZE]{};
};

inline void encoderRoundTripPopulatesAllFields(CrashEncoderTestSuite* suite) {
    CrashRecord rec = makeFullRecord();

    size_t len = CrashEncoder::encode(rec, suite->mac, suite->buf, sizeof(suite->buf));
    ASSERT_GT(len, 0u);
    ASSERT_LE(len, CrashEncoder::MAX_ENCODED_SIZE);

    alleycat_device_WriteDeviceLogRequest msg = decodeBytes(suite->buf, len);

    EXPECT_TRUE(msg.has_device_mac);
    EXPECT_STREQ(msg.device_mac, "aa:bb:cc:dd:ee:ff");
    EXPECT_TRUE(msg.has_crash_number);
    EXPECT_EQ(msg.crash_number, static_cast<uint64_t>(rec.crashNumber));
    EXPECT_TRUE(msg.has_uptime_ms);
    EXPECT_EQ(msg.uptime_ms, static_cast<uint64_t>(rec.timestamp));
    EXPECT_TRUE(msg.has_software_version);
    EXPECT_STREQ(msg.software_version, rec.commitHash);
    EXPECT_TRUE(msg.has_reset_reason);
    EXPECT_TRUE(msg.has_program_counter);
    EXPECT_TRUE(msg.has_exception_cause);
    EXPECT_TRUE(msg.has_task_name);
}

inline void encoderOmitsUnsetOptionalFields(CrashEncoderTestSuite* suite) {
    CrashRecord rec = makeFullRecord();
    rec.programCounter = 0;
    rec.exceptionCause = 0;
    rec.taskName[0] = '\0';

    size_t len = CrashEncoder::encode(rec, nullptr, suite->buf, sizeof(suite->buf));
    ASSERT_GT(len, 0u);

    alleycat_device_WriteDeviceLogRequest msg = decodeBytes(suite->buf, len);

    EXPECT_FALSE(msg.has_device_mac);
    EXPECT_FALSE(msg.has_program_counter);
    EXPECT_FALSE(msg.has_exception_cause);
    EXPECT_FALSE(msg.has_task_name);
}

inline void encoderReturnZeroWhenBufferTooSmall(CrashEncoderTestSuite* suite) {
    CrashRecord rec = makeFullRecord();
    uint8_t tinyBuf[4]{};

    size_t len = CrashEncoder::encode(rec, suite->mac, tinyBuf, sizeof(tinyBuf));

    EXPECT_EQ(len, 0u);
}
