#pragma once

#include <cstddef>
#include <cstdint>

#include "device/crash/crash-record.hpp"
#include "device/drivers/peer-comms-types.hpp"
#include "game/match.hpp"

/// What a record frame carries; also the first byte a record tag covers.
enum class RecordKind : uint8_t {
    CRASH = 0x01,
    MATCH = 0x02,
};

constexpr size_t CRASH_RECORD_SIZE = sizeof(CrashPacket);
constexpr size_t MATCH_RECORD_SIZE = sizeof(MatchRecord);
constexpr size_t RECORD_TAG_SIZE = 16;
constexpr size_t RECORD_TRAILER_SIZE = 17;
constexpr size_t RECORD_CHECK_SIZE = 8;

static_assert(CRASH_RECORD_SIZE == 42, "a crash record frame carries one 42-byte CrashPacket");
static_assert(MATCH_RECORD_SIZE == 112, "a match record frame carries one 112-byte MatchRecord");

/// Key ID and record tag. Key ID 0 with 16 zero bytes means "no record key".
struct RecordTrailer {
    uint8_t keyId;
    uint8_t tag[RECORD_TAG_SIZE];
} __attribute__((packed));
static_assert(sizeof(RecordTrailer) == RECORD_TRAILER_SIZE, "RecordTrailer wire size");

/// PDN-to-FDN record frame. recordNumber is the crash number or the match's sequence number.
template <size_t RECORD_SIZE> struct RecordFramePacket {
    uint8_t seqId;
    uint8_t kind;
    uint32_t recordNumber;
    uint8_t recordBytes[RECORD_SIZE];
    RecordTrailer trailer;
} __attribute__((packed));

/// Over PktType::kCrashRecord.
using CrashRecordFramePacket = RecordFramePacket<CRASH_RECORD_SIZE>;
static_assert(sizeof(CrashRecordFramePacket) == 65, "CrashRecordFramePacket wire size");

/// Over PktType::kMatchRecord.
using MatchRecordFramePacket = RecordFramePacket<MATCH_RECORD_SIZE>;
static_assert(sizeof(MatchRecordFramePacket) == 135, "MatchRecordFramePacket wire size");

// Everything that has to agree for one record kind, so that a relay lane, an
// FDN ring or a record source is written against a kind and cannot mix one
// kind's packet types or record size with another's.
struct CrashRecordTraits {
    static constexpr RecordKind KIND = RecordKind::CRASH;
    static constexpr size_t RECORD_SIZE = CRASH_RECORD_SIZE;
    static constexpr PktType FRAME_PACKET_TYPE = PktType::kCrashRecord;
    static constexpr PktType ACK_PACKET_TYPE = PktType::kCrashRecordAck;
    using Frame = CrashRecordFramePacket;
};

struct MatchRecordTraits {
    static constexpr RecordKind KIND = RecordKind::MATCH;
    static constexpr size_t RECORD_SIZE = MATCH_RECORD_SIZE;
    static constexpr PktType FRAME_PACKET_TYPE = PktType::kMatchRecord;
    static constexpr PktType ACK_PACKET_TYPE = PktType::kMatchRecordAck;
    using Frame = MatchRecordFramePacket;
};

/// FDN-to-PDN over PktType::kCrashRecordAck or kMatchRecordAck: the record is in
/// the FDN's non-volatile storage. recordCheck names the exact frame that was stored.
struct RecordAckPacket {
    uint8_t seqId;
    uint8_t kind;
    uint32_t recordNumber;
    uint8_t recordCheck[RECORD_CHECK_SIZE];
} __attribute__((packed));
static_assert(sizeof(RecordAckPacket) == 14, "RecordAckPacket wire size");

/// Writes the record check: the first RECORD_CHECK_SIZE bytes of SHA-256 over
/// the record bytes followed by the trailer. It only identifies a frame; it is
/// not keyed and proves nothing about who computed it.
void computeRecordCheck(const uint8_t* recordBytes, size_t recordLength,
                        const RecordTrailer& trailer, uint8_t (&recordCheck)[RECORD_CHECK_SIZE]);
