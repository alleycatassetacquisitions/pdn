#pragma once

#include <cstddef>
#include <cstdint>

#include "device/crash/crash-record.hpp"
#include "device/drivers/peer-comms-types.hpp"
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

/// A match record frame's content is the 112 record bytes, then their 16-byte
/// tag; a crash record carries no tag.
using MatchRecordFramePacket = RecordFramePacket<sizeof(TaggedMatchRecord)>;
static_assert(sizeof(MatchRecordFramePacket) == 134, "MatchRecordFramePacket wire size");

// What has to agree for one record kind, so a relay lane or a record source is
// written against a kind and cannot mix one kind's packet type or size with another's.
struct CrashRecordTraits {
    static constexpr RecordKind KIND = RecordKind::CRASH;
    static constexpr size_t RECORD_SIZE = CRASH_RECORD_SIZE;
    static constexpr PktType FRAME_PACKET_TYPE = PktType::kCrashRecord;
    using Frame = CrashRecordFramePacket;
};

struct MatchRecordTraits {
    static constexpr RecordKind KIND = RecordKind::MATCH;
    static constexpr size_t RECORD_SIZE = sizeof(TaggedMatchRecord);
    static constexpr PktType FRAME_PACKET_TYPE = PktType::kMatchRecord;
    using Frame = MatchRecordFramePacket;
};

/// FDN-to-PDN over kRecordAck, sent only once the record is in the FDN's NVS.
/// contentCrc is crc16() over the content, a match record's tag included, and
/// names the exact frame stored; it identifies, it does not authenticate.
struct RecordAckPacket {
    uint8_t seqId;
    uint8_t kind;
    uint32_t recordNumber;
    uint16_t contentCrc;
} __attribute__((packed));
static_assert(sizeof(RecordAckPacket) == 8, "RecordAckPacket wire size");
