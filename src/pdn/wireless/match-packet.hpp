#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "id-generator.hpp"
#include "utils/uuid-bytes.hpp"

// Per-match key the Hunter draws from the hardware RNG and sends over the cable
// (serial application frame MATCH_KEY); both sides sign their standalone match
// frames with it.
inline constexpr size_t MATCH_KEY_SIZE = 16;
// The first 16 bytes of HMAC-SHA-256, truncated as the record tag is.
inline constexpr size_t MATCH_TAG_SIZE = 16;

using MatchKey = std::array<uint8_t, MATCH_KEY_SIZE>;

// MATCH_KEY application frame: the match ID's 16 bytes, then the key.
inline constexpr size_t MATCH_KEY_FRAME_ID_OFFSET = 0;
inline constexpr size_t MATCH_KEY_FRAME_KEY_OFFSET = UUIDBytes::SIZE;

struct MatchPacket {
    char matchId[IdGenerator::UUID_BUFFER_SIZE];
    UUIDBytes playerUuid;
    UUIDBytes teamUuid;
    uint8_t isHunter;
    long playerDrawTime;  // raw reaction time, before any boost
    uint32_t boostMs;     // posse boost the sender earned; 0 without supporters
    int command;
    // Signed standalone frames: rises by one per frame the sender sends in the
    // match, from 1; 0 with the tag zero on an unsigned frame.
    uint32_t counter;
    uint8_t tag[MATCH_TAG_SIZE];
    uint8_t seqId;
} __attribute__((packed));

// 103 bytes on the ESP32, whose long is 4 bytes; a frame of any other length
// fails the channel's length check, so an older build's 83-byte frames are dropped.
static_assert(sizeof(MatchPacket) == 99 + sizeof(long), "MatchPacket wire layout changed");

namespace MatchCmd {
constexpr int SEND_MATCH_ID = 6;
constexpr int MATCH_ID_ACK = 7;
constexpr int MATCH_ROLE_MISMATCH = 8;
constexpr int DRAW_RESULT = 9;
constexpr int NEVER_PRESSED = 10;
}

/**
 * Writes @p packet's tag: the first MATCH_TAG_SIZE bytes of HMAC-SHA-256 under
 * @p key over the kMatchCommand type byte and the packet with seqId and tag
 * zeroed (seqId is stamped after signing, by the channel).
 */
void signMatchPacket(const MatchKey& key, MatchPacket& packet);

/** True when @p packet carries the tag signMatchPacket() writes under @p key; compared in constant
 * time. */
bool matchPacketTagVerifies(const MatchKey& key, const MatchPacket& packet);

struct MatchCommand {
    const uint8_t* wifiMacAddr;
    int command;
    char matchId[IdGenerator::UUID_BUFFER_SIZE];
    UUIDBytes playerUuid;
    UUIDBytes teamUuid;
    bool isHunter;
    long playerDrawTime;
    unsigned long boostMs = 0;
    // SEND_MATCH_ID only: the key the initiation was verified under and its
    // counter. Null when no key off the cable verified it, and such an
    // initiation primes no match.
    const MatchKey* matchKey = nullptr;
    uint32_t counter = 0;

    /** Decoded match frame received from macAddress. */
    MatchCommand(const uint8_t* macAddress, int command, const char* matchId, long playerDrawTime,
                 bool isHunter, const UUIDBytes& playerUuid = UUIDBytes{},
                 const UUIDBytes& teamUuid = UUIDBytes{})
        : wifiMacAddr(macAddress)
        , command(command)
        , playerUuid(playerUuid)
        , teamUuid(teamUuid)
        , isHunter(isHunter)
        , playerDrawTime(playerDrawTime) {
        IdGenerator::copyId(this->matchId, matchId);
    }
};
