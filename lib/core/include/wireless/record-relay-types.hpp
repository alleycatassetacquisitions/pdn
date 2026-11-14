#pragma once

#include <cstddef>
#include <cstdint>

#include "device/crash/crash-record.hpp"
#include "device/drivers/peer-comms-types.hpp"
#include "game/match.hpp"

/// What a record frame carries.
enum class RecordKind : uint8_t {
    CRASH = 0x01,
    MATCH = 0x02,
};

constexpr size_t CRASH_RECORD_SIZE = sizeof(CrashPacket);
constexpr size_t MATCH_RECORD_SIZE = sizeof(MatchRecord);
constexpr size_t RECORD_TAG_SIZE = 16;
constexpr size_t RECORD_CHECK_SIZE = 8;

static_assert(CRASH_RECORD_SIZE == 42, "a crash record frame carries one 42-byte CrashPacket");
static_assert(MATCH_RECORD_SIZE == 112, "a match record frame carries one 112-byte MatchRecord");

// A kind's content is what a record source hands out; the frame, the record
// check and the FDN's stored entry carry it unchanged.

/// A crash record carries no tag: nothing verifies a crash record.
struct CrashRecordContent {
    uint8_t recordBytes[CRASH_RECORD_SIZE];
} __attribute__((packed));

/// A match record and its tag. The FDN stores the tag without reading it.
struct MatchRecordContent {
    uint8_t recordBytes[MATCH_RECORD_SIZE];
    uint8_t tag[RECORD_TAG_SIZE];
} __attribute__((packed));
static_assert(sizeof(CrashRecordContent) == CRASH_RECORD_SIZE, "a crash record is bytes alone");
static_assert(sizeof(MatchRecordContent) == MATCH_RECORD_SIZE + RECORD_TAG_SIZE,
              "a match record is its bytes, then its tag");

/// PDN-to-FDN record frame. recordNumber is the crash number or the match's sequence number.
template <class Content> struct RecordFramePacket {
    uint8_t seqId;
    uint8_t kind;
    uint32_t recordNumber;
    Content content;
} __attribute__((packed));

/// Over PktType::kCrashRecord.
using CrashRecordFramePacket = RecordFramePacket<CrashRecordContent>;
static_assert(sizeof(CrashRecordFramePacket) == 48, "CrashRecordFramePacket wire size");

/// Over PktType::kMatchRecord.
using MatchRecordFramePacket = RecordFramePacket<MatchRecordContent>;
static_assert(sizeof(MatchRecordFramePacket) == 134, "MatchRecordFramePacket wire size");

// Everything that has to agree for one record kind, so that a relay lane, an
// FDN ring or a record source is written against a kind and cannot mix one
// kind's packet types or record layout with another's. Content is the one
// place a kind states its size and whether it carries a tag.
struct CrashRecordTraits {
    static constexpr RecordKind KIND = RecordKind::CRASH;
    static constexpr PktType FRAME_PACKET_TYPE = PktType::kCrashRecord;
    static constexpr PktType ACK_PACKET_TYPE = PktType::kCrashRecordAck;
    using Content = CrashRecordContent;
    using Frame = CrashRecordFramePacket;
};

struct MatchRecordTraits {
    static constexpr RecordKind KIND = RecordKind::MATCH;
    static constexpr PktType FRAME_PACKET_TYPE = PktType::kMatchRecord;
    static constexpr PktType ACK_PACKET_TYPE = PktType::kMatchRecordAck;
    using Content = MatchRecordContent;
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

/// Writes the first RECORD_CHECK_SIZE bytes of SHA-256 over @p length bytes at @p content.
void computeRecordCheck(const uint8_t* content, size_t length,
                        uint8_t (&recordCheck)[RECORD_CHECK_SIZE]);

/// Writes the record check of @p content: over the record bytes, followed by
/// the tag for a match record. It only identifies a frame; it is not keyed and
/// proves nothing about who computed it.
template <class Content>
void computeRecordCheck(const Content& content, uint8_t (&recordCheck)[RECORD_CHECK_SIZE]) {
    computeRecordCheck(reinterpret_cast<const uint8_t*>(&content), sizeof(content), recordCheck);
}
