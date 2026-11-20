#pragma once

#include <gtest/gtest.h>
#include <cstring>
#include <pb_decode.h>

#include "alleycat-server/crash-codec.hpp"
#include "alleycat-server/device_api.pb.h"
#include "device/crash/crash-record.hpp"

inline CrashRecord makeFullRecord() {
    CrashRecord rec{};
    rec.crashNumber    = 3;
    rec.timestamp      = 12500;
    rec.resetReason    = 7;
    CoreDumpDetails& details = rec.coreDumpDetails.emplace();
    details.programCounter = 0x400D1A2C;
    details.exceptionCause = 28;
    strncpy(details.taskName, "loopTask", TASK_NAME_LENGTH - 1);
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

class CrashCodecTestSuite : public testing::Test {
public:
    const uint8_t mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    uint8_t buf[CrashCodec::MAX_ENCODED_SIZE]{};
};

inline void crashCodecRoundTripPopulatesAllFields(CrashCodecTestSuite* suite) {
    CrashRecord rec = makeFullRecord();

    size_t len = CrashCodec::encode(rec, suite->mac, suite->buf, sizeof(suite->buf));
    ASSERT_GT(len, 0u);
    ASSERT_LE(len, CrashCodec::MAX_ENCODED_SIZE);

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

inline void crashCodecOmitsUnsetOptionalFields(CrashCodecTestSuite* suite) {
    CrashRecord rec = makeFullRecord();
    rec.coreDumpDetails.reset();

    size_t len = CrashCodec::encode(rec, nullptr, suite->buf, sizeof(suite->buf));
    ASSERT_GT(len, 0u);

    alleycat_device_WriteDeviceLogRequest msg = decodeBytes(suite->buf, len);

    EXPECT_FALSE(msg.has_device_mac);
    EXPECT_FALSE(msg.has_program_counter);
    EXPECT_FALSE(msg.has_exception_cause);
    EXPECT_FALSE(msg.has_task_name);
}

inline void crashCodecReturnZeroWhenBufferTooSmall(CrashCodecTestSuite* suite) {
    CrashRecord rec = makeFullRecord();
    uint8_t tinyBuf[4]{};

    size_t len = CrashCodec::encode(rec, suite->mac, tinyBuf, sizeof(tinyBuf));

    EXPECT_EQ(len, 0u);
}

inline void crashCodecKeepsExceptionCauseZero(CrashCodecTestSuite* suite) {
    CrashRecord rec = makeFullRecord();
    rec.coreDumpDetails->exceptionCause = 0;  // IllegalInstruction

    size_t len = CrashCodec::encode(rec, suite->mac, suite->buf, sizeof(suite->buf));
    ASSERT_GT(len, 0u);

    alleycat_device_WriteDeviceLogRequest msg = decodeBytes(suite->buf, len);

    EXPECT_TRUE(msg.has_exception_cause);
    EXPECT_EQ(msg.exception_cause, 0u);
    EXPECT_TRUE(msg.has_task_name);
}

inline void crashCodecOmitsMissingExceptionCause(CrashCodecTestSuite* suite) {
    CrashRecord rec = makeFullRecord();
    rec.coreDumpDetails->exceptionCause.reset();

    size_t len = CrashCodec::encode(rec, suite->mac, suite->buf, sizeof(suite->buf));
    ASSERT_GT(len, 0u);

    alleycat_device_WriteDeviceLogRequest msg = decodeBytes(suite->buf, len);

    EXPECT_TRUE(msg.has_program_counter);
    EXPECT_FALSE(msg.has_exception_cause);
    EXPECT_TRUE(msg.has_task_name);
}

// A stored packet from a PDN: details, an unterminated task name, the sender's MAC.
inline void crashCodecEncodesStoredPacketUnderSenderMac(CrashCodecTestSuite* suite) {
    CrashPacket packet{};
    packet.crashNumber = 9;
    packet.timestamp = 4000;
    packet.resetReason = 4;
    packet.programCounter = 0x400D1A2C;
    packet.exceptionCause = 0;
    memset(packet.taskName, 'T', sizeof(packet.taskName));
    memcpy(packet.commitHash, "a1b2c3d4", COMMIT_HASH_LENGTH - 1);
    const uint8_t senderMac[6] = {0x0A, 0x01, 0x02, 0x03, 0x04, 0x05};

    size_t len =
    CrashCodec::encode(fromCrashPacket(packet), senderMac, suite->buf, sizeof(suite->buf));
    ASSERT_GT(len, 0u);
    alleycat_device_WriteDeviceLogRequest msg = decodeBytes(suite->buf, len);

    EXPECT_STREQ(msg.device_mac, "0a:01:02:03:04:05");
    EXPECT_EQ(msg.crash_number, 9u);
    EXPECT_EQ(msg.uptime_ms, 4000u);
    EXPECT_EQ(msg.reset_reason, 4u);
    EXPECT_EQ(msg.program_counter, 0x400D1A2Cu);
    EXPECT_TRUE(msg.has_exception_cause) << "cause 0 is a real cause";
    EXPECT_EQ(std::string(msg.task_name), std::string(TASK_NAME_LENGTH - 1, 'T'));
    EXPECT_STREQ(msg.software_version, "a1b2c3d4");
}

inline void crashCodecStoredPacketWithoutDetailsOmitsThem(CrashCodecTestSuite* suite) {
    CrashPacket packet{};
    packet.crashNumber = 2;
    packet.programCounter = 5;  // with an empty task name it is not a detail
    size_t len =
    CrashCodec::encode(fromCrashPacket(packet), suite->mac, suite->buf, sizeof(suite->buf));
    ASSERT_GT(len, 0u);
    alleycat_device_WriteDeviceLogRequest msg = decodeBytes(suite->buf, len);
    EXPECT_FALSE(msg.has_program_counter);
    EXPECT_FALSE(msg.has_task_name);

    memcpy(packet.taskName, "loopTask", 8);
    packet.exceptionCause = CRASH_PACKET_NO_EXCEPTION_CAUSE;
    len = CrashCodec::encode(fromCrashPacket(packet), suite->mac, suite->buf, sizeof(suite->buf));
    msg = decodeBytes(suite->buf, len);
    EXPECT_TRUE(msg.has_program_counter);
    EXPECT_FALSE(msg.has_exception_cause);
}

// exceptionCause is at offset 13, so in a 4-byte-aligned packet it is misaligned. This test pins
// the values read back; only a clang UBSan build detects a reference bound to that field.
inline void crashPacketReadsDetailsOffAnUnalignedPacket() {
    alignas(uint32_t) CrashPacket packet{};
    packet.crashNumber = 0x01020304;
    packet.timestamp = 0x05060708;
    packet.resetReason = 4;
    packet.programCounter = 0x42001234;
    packet.exceptionCause = 0x1C2D3E4F;
    memcpy(packet.taskName, "loopTask", 8);
    memcpy(packet.commitHash, "a1b2c3d4", COMMIT_HASH_LENGTH - 1);
    ASSERT_NE((reinterpret_cast<uintptr_t>(&packet) + offsetof(CrashPacket, exceptionCause)) %
              alignof(uint32_t),
              0u);

    CrashRecord record = fromCrashPacket(packet);

    EXPECT_EQ(record.crashNumber, 0x01020304u);
    EXPECT_EQ(record.timestamp, 0x05060708u);
    EXPECT_EQ(record.resetReason, 4u);
    ASSERT_TRUE(record.coreDumpDetails.has_value());
    EXPECT_EQ(record.coreDumpDetails->programCounter, 0x42001234u);
    ASSERT_TRUE(record.coreDumpDetails->exceptionCause.has_value());
    EXPECT_EQ(*record.coreDumpDetails->exceptionCause, 0x1C2D3E4Fu);
    EXPECT_STREQ(record.coreDumpDetails->taskName, "loopTask");
    EXPECT_STREQ(record.commitHash, "a1b2c3d4");

    packet.exceptionCause = CRASH_PACKET_NO_EXCEPTION_CAUSE;
    record = fromCrashPacket(packet);
    ASSERT_TRUE(record.coreDumpDetails.has_value());
    EXPECT_FALSE(record.coreDumpDetails->exceptionCause.has_value());
}
