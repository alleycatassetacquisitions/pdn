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

// The packet is what a PDN sends over ESP-NOW and an FDN keeps in NVS, so its bytes are the wire
// format: a layout change would misread records from other firmware or already stored.
inline void crashPacketCarriesRecordInRelayLayout() {
    const uint8_t expected[] = {
    0x03, 0x00, 0x00, 0x00,  // crash number 3
    0xD4, 0x30, 0x00, 0x00,  // uptime 12500 ms
    0x07,                    // reset reason
    0x2C, 0x1A, 0x0D, 0x40,  // program counter at offset 9
    0x1C, 0x00, 0x00, 0x00,  // exception cause 28 at offset 13
    'l',  'o',  'o',  'p',  'T', 'a', 's', 'k', 0, 0, 0, 0, 0, 0, 0, 0,  // task name
    'a',  '1',  'b',  '2',  'c', '3', 'd', '4', 0,                       // commit hash
    };
    ASSERT_EQ(sizeof(CrashPacket), sizeof(expected));

    CrashPacket packet = toCrashPacket(makeFullRecord());

    uint8_t actual[sizeof(CrashPacket)];
    memcpy(actual, &packet, sizeof(actual));
    for (size_t i = 0; i < sizeof(expected); i++) {
        EXPECT_EQ(actual[i], expected[i]) << "byte " << i;
    }
}

// A crash relayed PDN to FDN reaches the server with the values the PDN recorded; the server
// dedupes on MAC and crash number.
inline void crashRelayedThroughPacketReachesServerUnchanged(CrashCodecTestSuite* suite) {
    CrashRecord rec = makeFullRecord();

    CrashRecord relayed = fromCrashPacket(toCrashPacket(rec));
    size_t len = CrashCodec::encode(relayed, suite->mac, suite->buf, sizeof(suite->buf));
    ASSERT_GT(len, 0u);
    alleycat_device_WriteDeviceLogRequest msg = decodeBytes(suite->buf, len);

    EXPECT_EQ(msg.crash_number, 3u);
    EXPECT_EQ(msg.uptime_ms, 12500u);
    EXPECT_EQ(msg.reset_reason, 7u);
    EXPECT_EQ(msg.program_counter, 0x400D1A2Cu);
    ASSERT_TRUE(msg.has_exception_cause);
    EXPECT_EQ(msg.exception_cause, 28u);
    EXPECT_STREQ(msg.task_name, "loopTask");
    EXPECT_STREQ(msg.software_version, "a1b2c3d4");
}

// A task watchdog crash has no exception cause; relayed, it must not arrive as cause 0
// (IllegalInstruction).
inline void crashWithoutCauseRelaysWithoutCause(CrashCodecTestSuite* suite) {
    CrashRecord rec = makeFullRecord();
    rec.coreDumpDetails->exceptionCause.reset();

    CrashRecord relayed = fromCrashPacket(toCrashPacket(rec));
    size_t len = CrashCodec::encode(relayed, suite->mac, suite->buf, sizeof(suite->buf));
    ASSERT_GT(len, 0u);
    alleycat_device_WriteDeviceLogRequest msg = decodeBytes(suite->buf, len);

    EXPECT_TRUE(msg.has_program_counter);
    EXPECT_EQ(msg.program_counter, 0x400D1A2Cu);
    EXPECT_FALSE(msg.has_exception_cause);
    EXPECT_STREQ(msg.task_name, "loopTask");
}
