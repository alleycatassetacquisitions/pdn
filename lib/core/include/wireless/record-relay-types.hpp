#pragma once

#include <cstddef>
#include <cstdint>

#include "device/crash/crash-record.hpp"
#include "game/match.hpp"

/// What a record frame carries. The kind byte travels in the frame so the ack
/// can name it; each kind has its own packet type as well.
enum class RecordKind : uint8_t {
    CRASH = 0x01,
    MATCH = 0x02,
};

constexpr size_t CRASH_RECORD_SIZE = sizeof(CrashPacket);
static_assert(CRASH_RECORD_SIZE == 42, "a crash record frame carries one 42-byte CrashPacket");

/// PDN-to-FDN record frame: kCrashRecord or kMatchRecord. recordNumber is the
/// crash number or the match's sequence number on the sending PDN.
template <size_t CONTENT_SIZE> struct RecordFramePacket {
    uint8_t seqId;
    uint8_t kind;
    uint32_t recordNumber;
    uint8_t content[CONTENT_SIZE];
} __attribute__((packed));

using CrashRecordFramePacket = RecordFramePacket<CRASH_RECORD_SIZE>;
static_assert(sizeof(CrashRecordFramePacket) == 48, "CrashRecordFramePacket wire size");

using MatchRecordFramePacket = RecordFramePacket<MATCH_BINARY_SIZE>;
static_assert(sizeof(MatchRecordFramePacket) == 118, "MatchRecordFramePacket wire size");

/// FDN-to-PDN over kRecordAck, sent only once the record is in the FDN's NVS.
/// contentCrc is crc16() over the content and names the exact frame stored; it
/// identifies, it does not authenticate.
struct RecordAckPacket {
    uint8_t seqId;
    uint8_t kind;
    uint32_t recordNumber;
    uint16_t contentCrc;
} __attribute__((packed));
static_assert(sizeof(RecordAckPacket) == 8, "RecordAckPacket wire size");
