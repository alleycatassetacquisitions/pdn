#pragma once

#include <gtest/gtest.h>
#include <cstring>
#include <pb_decode.h>

#include "alleycat-server/crash-codec.hpp"
#include "alleycat-server/device_api.pb.h"
#include "alleycat-server/fdn-stored-entry.hpp"
#include "device/crash/crash-record.hpp"
#include "wireless/record-relay-types.hpp"

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

/** The stored entry of @p packet as the device 10:20:30:40:50:60 sent it. */
inline FdnStoredCrashEntry makeStoredCrashEntry(const CrashPacket& packet) {
    FdnStoredCrashEntry entry{};
    const uint8_t senderMac[6] = {0x10, 0x20, 0x30, 0x40, 0x50, 0x60};
    memcpy(entry.senderMac, senderMac, sizeof(entry.senderMac));
    entry.kind = static_cast<uint8_t>(RecordKind::CRASH);
    entry.recordNumber = packet.crashNumber;
    memcpy(entry.content.recordBytes, &packet, sizeof(packet));
    return entry;
}

/** The stored entry of @p record as the device 10:20:30:40:50:60 sent it. */
inline FdnStoredCrashEntry makeStoredCrashEntry(const CrashRecord& record) {
    return makeStoredCrashEntry(toCrashPacket(record));
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
    uint8_t buf[CrashCodec::MAX_ENCODED_SIZE]{};
};

inline void crashCodecRoundTripPopulatesAllFields(CrashCodecTestSuite* suite) {
    const FdnStoredCrashEntry entry = makeStoredCrashEntry(makeFullRecord());

    size_t len = CrashCodec::encode(entry, suite->buf, sizeof(suite->buf));
    ASSERT_GT(len, 0u);
    ASSERT_LE(len, CrashCodec::MAX_ENCODED_SIZE);

    alleycat_device_WriteDeviceLogRequest msg = decodeBytes(suite->buf, len);

    EXPECT_TRUE(msg.has_device_mac);
    EXPECT_STREQ(msg.device_mac, "10:20:30:40:50:60")
    << "the device is the one the entry says sent the record";
    EXPECT_TRUE(msg.has_crash_number);
    EXPECT_EQ(msg.crash_number, 3u);
    EXPECT_TRUE(msg.has_uptime_ms);
    EXPECT_EQ(msg.uptime_ms, 12500u);
    EXPECT_TRUE(msg.has_software_version);
    EXPECT_STREQ(msg.software_version, "a1b2c3d4");
    EXPECT_TRUE(msg.has_reset_reason);
    EXPECT_EQ(msg.reset_reason, 7u);
    EXPECT_TRUE(msg.has_program_counter);
    EXPECT_EQ(msg.program_counter, 0x400D1A2Cu);
    EXPECT_TRUE(msg.has_exception_cause);
    EXPECT_EQ(msg.exception_cause, 28u);
    EXPECT_TRUE(msg.has_task_name);
    EXPECT_STREQ(msg.task_name, "loopTask");
}

inline void crashCodecOmitsUnsetOptionalFields(CrashCodecTestSuite* suite) {
    CrashRecord rec = makeFullRecord();
    rec.coreDumpDetails.reset();

    size_t len = CrashCodec::encode(makeStoredCrashEntry(rec), suite->buf, sizeof(suite->buf));
    ASSERT_GT(len, 0u);

    alleycat_device_WriteDeviceLogRequest msg = decodeBytes(suite->buf, len);

    EXPECT_FALSE(msg.has_program_counter);
    EXPECT_FALSE(msg.has_exception_cause);
    EXPECT_FALSE(msg.has_task_name);
    EXPECT_EQ(msg.crash_number, 3u);
}

inline void crashCodecReturnZeroWhenBufferTooSmall(CrashCodecTestSuite* suite) {
    (void)suite;
    uint8_t tinyBuf[4]{};

    size_t len =
    CrashCodec::encode(makeStoredCrashEntry(makeFullRecord()), tinyBuf, sizeof(tinyBuf));

    EXPECT_EQ(len, 0u);
}

inline void crashCodecKeepsExceptionCauseZero(CrashCodecTestSuite* suite) {
    CrashRecord rec = makeFullRecord();
    rec.coreDumpDetails->exceptionCause = 0;  // IllegalInstruction

    size_t len = CrashCodec::encode(makeStoredCrashEntry(rec), suite->buf, sizeof(suite->buf));
    ASSERT_GT(len, 0u);

    alleycat_device_WriteDeviceLogRequest msg = decodeBytes(suite->buf, len);

    EXPECT_TRUE(msg.has_exception_cause);
    EXPECT_EQ(msg.exception_cause, 0u);
    EXPECT_TRUE(msg.has_task_name);
}

inline void crashCodecOmitsMissingExceptionCause(CrashCodecTestSuite* suite) {
    CrashRecord rec = makeFullRecord();
    rec.coreDumpDetails->exceptionCause.reset();

    size_t len = CrashCodec::encode(makeStoredCrashEntry(rec), suite->buf, sizeof(suite->buf));
    ASSERT_GT(len, 0u);

    alleycat_device_WriteDeviceLogRequest msg = decodeBytes(suite->buf, len);

    EXPECT_TRUE(msg.has_program_counter);
    EXPECT_FALSE(msg.has_exception_cause);
    EXPECT_TRUE(msg.has_task_name);
}

// toCrashPacket leaves all three detail fields zero only for a record without
// details, so an empty task name alone must not drop the other two.
inline void crashCodecKeepsTheDetailsOfARecordWithAnEmptyTaskName(CrashCodecTestSuite* suite) {
    CrashRecord rec = makeFullRecord();
    rec.coreDumpDetails->taskName[0] = '\0';

    size_t len = CrashCodec::encode(makeStoredCrashEntry(rec), suite->buf, sizeof(suite->buf));
    ASSERT_GT(len, 0u);
    alleycat_device_WriteDeviceLogRequest msg = decodeBytes(suite->buf, len);
    EXPECT_TRUE(msg.has_program_counter) << "an empty task name dropped the program counter";
    EXPECT_EQ(msg.program_counter, 0x400D1A2Cu);
    EXPECT_TRUE(msg.has_exception_cause) << "an empty task name dropped the exception cause";
    EXPECT_EQ(msg.exception_cause, 28u);
    EXPECT_TRUE(msg.has_task_name);
    EXPECT_STREQ(msg.task_name, "");

    // The same with no exception cause, as after a task watchdog reset.
    rec.coreDumpDetails->exceptionCause.reset();
    len = CrashCodec::encode(makeStoredCrashEntry(rec), suite->buf, sizeof(suite->buf));
    ASSERT_GT(len, 0u);
    msg = decodeBytes(suite->buf, len);
    EXPECT_TRUE(msg.has_program_counter);
    EXPECT_EQ(msg.program_counter, 0x400D1A2Cu);
    EXPECT_FALSE(msg.has_exception_cause);
}

// The record bytes come off the radio from any sender, so a string in them
// may have no terminator; the largest record must still fit the buffer.
inline void crashCodecCutsAnUnterminatedString(CrashCodecTestSuite* suite) {
    CrashPacket packet{};
    memset(&packet, 0xFF, sizeof(packet));
    memset(packet.taskName, 'T', sizeof(packet.taskName));
    memset(packet.commitHash, 'c', sizeof(packet.commitHash));

    size_t len = CrashCodec::encode(makeStoredCrashEntry(packet), suite->buf, sizeof(suite->buf));
    ASSERT_GT(len, 0u) << "the largest record does not fit MAX_ENCODED_SIZE";
    ASSERT_LE(len, CrashCodec::MAX_ENCODED_SIZE);

    alleycat_device_WriteDeviceLogRequest msg = decodeBytes(suite->buf, len);
    EXPECT_STREQ(msg.task_name, "TTTTTTTTTTTTTTT");
    EXPECT_STREQ(msg.software_version, "cccccccc");
    EXPECT_EQ(msg.crash_number, 0xFFFFFFFFu);
    EXPECT_EQ(msg.uptime_ms, 0xFFFFFFFFu);
    EXPECT_EQ(msg.reset_reason, 0xFFu);
    EXPECT_EQ(msg.program_counter, 0xFFFFFFFFu);
    EXPECT_EQ(msg.exception_cause, 0xFFFFFFFFu);
}

// A proto3 string must be UTF-8, and the server refuses a body whose string is
// not. One forged frame with such a byte must not become a body the server
// refuses for good.
inline void crashCodecReplacesBytesThatAreNotPrintableAscii(CrashCodecTestSuite* suite) {
    CrashPacket packet = toCrashPacket(makeFullRecord());
    const char taskName[TASK_NAME_LENGTH] = {'l',    '\xC3', '(', 'p',    '\x01',
                                             '\x7F', ' ',    '~', '\xFF', '\0'};
    memcpy(packet.taskName, taskName, sizeof(packet.taskName));
    const char commitHash[COMMIT_HASH_LENGTH] = {'\x80', '1',    '\t', '2', '\x1F',
                                                 '3',    '\xE2', '4',  '\0'};
    memcpy(packet.commitHash, commitHash, sizeof(packet.commitHash));

    size_t len = CrashCodec::encode(makeStoredCrashEntry(packet), suite->buf, sizeof(suite->buf));
    ASSERT_GT(len, 0u);
    alleycat_device_WriteDeviceLogRequest msg = decodeBytes(suite->buf, len);

    EXPECT_STREQ(msg.task_name, "l?(p?? ~?")
    << "printable bytes, space and '~' included, stay; every other byte is replaced";
    EXPECT_STREQ(msg.software_version, "?1?2?3?4");
    EXPECT_EQ(msg.crash_number, 3u);
    EXPECT_EQ(msg.program_counter, 0x400D1A2Cu);
}
