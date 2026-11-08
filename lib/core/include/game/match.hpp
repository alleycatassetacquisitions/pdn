#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include "../../include/id-generator.hpp"
#include "utils/uuid-bytes.hpp"

// Player ID is stored as 4 raw bytes in the legacy short-ID slot
constexpr size_t PLAYER_ID_BINARY_SIZE = 4;
constexpr size_t MAC_BINARY_SIZE = 6;

// Binary layout, identical on every platform because NVS records and
// kMatchCollection frames cross between PDN and FDN builds:
// - matchUuid (16B)
// - legacy hunter/bounty short IDs (2 * 4B)
// - hunter/bounty draw times (2 * uint32_t)
// - hunterPlayerId, bountyPlayerId, hunterTeamId, bountyTeamId, winnerPlayerId (5 * 16B)
// - hunterMac, bountyMac (2 * 6B)
// - occurredAtUnixMs (int64_t)
constexpr size_t MATCH_BINARY_SIZE = IdGenerator::UUID_BINARY_SIZE + 2 * PLAYER_ID_BINARY_SIZE +
                                     2 * sizeof(uint32_t) + 5 * UUIDBytes::SIZE +
                                     2 * MAC_BINARY_SIZE + sizeof(int64_t);

/** PDN-to-FDN stored match over PktType::kMatchCollection. matchSeq is the PDN's ring sequence. */
struct MatchRecordPacket {
    uint32_t matchSeq;
    uint8_t matchData[MATCH_BINARY_SIZE];
} __attribute__((packed));

/** FDN-to-PDN over PktType::kMatchCollectionAck: the record with matchSeq is persisted. */
struct MatchRecordAckPacket {
    uint32_t matchSeq;
} __attribute__((packed));

/**
 * Represents a match between a hunter and bounty player.
 * Stores match details and outcome, with serialization support.
 */
class Match {
public:
    /**
     * Creates an empty match: blank ids, both draw times zero.
     */
    Match() = default;

    /**
     * Creates a new match from raw C-strings — no heap allocation in the hot path.
     */
    Match(const char* matchId, const char* playerId, bool isHunter);

    /**
     * Deleted: three strings would otherwise bind to the overload above, converting
     * the bounty id to `true` and leaving the bounty unset.
     */
    Match(const char* matchId, const char* hunterId, const char* bountyId) = delete;

    /**
     * Sets the hunter's draw time
     * @param timeMs draw time in milliseconds
     */
    void setHunterDrawTime(unsigned long timeMs) { hunterDrawTimeMs = timeMs; }

    /**
     * Sets the bounty's draw time
     * @param timeMs draw time in milliseconds
     */
    void setBountyDrawTime(unsigned long timeMs) { bountyDrawTimeMs = timeMs; }

    /**
     * Sets the hunter's ID
     * @param hunterId Hunter player's ID
     */
    void setHunterId(const char* hunterId);

    /**
     * Sets the bounty's ID
     * @param bountyId Bounty player's ID
     */
    void setBountyId(const char* bountyId);

    /** Sets the 16-byte hunter player UUID. */
    void setHunterPlayerId(const UUIDBytes& id) { hunterPlayerId = id; }
    /** Sets the 16-byte bounty player UUID. */
    void setBountyPlayerId(const UUIDBytes& id) { bountyPlayerId = id; }
    /** Sets the 16-byte hunter team UUID. */
    void setHunterTeamId(const UUIDBytes& id) { hunterTeamId = id; }
    /** Sets the 16-byte bounty team UUID. */
    void setBountyTeamId(const UUIDBytes& id) { bountyTeamId = id; }
    /** Sets the 16-byte winning player UUID. */
    void setWinnerPlayerId(const UUIDBytes& id) { winnerPlayerId = id; }

    /** Sets the 6-byte hunter device MAC address. */
    void setHunterMac(const uint8_t* mac) {
        if (mac) {
            memcpy(hunterMac, mac, MAC_BINARY_SIZE);
        } else {
            memset(hunterMac, 0, MAC_BINARY_SIZE);
        }
    }

    /** Sets the 6-byte bounty device MAC address. */
    void setBountyMac(const uint8_t* mac) {
        if (mac) {
            memcpy(bountyMac, mac, MAC_BINARY_SIZE);
        } else {
            memset(bountyMac, 0, MAC_BINARY_SIZE);
        }
    }

    /** Sets the timestamp at which the match finalized (milliseconds). */
    void setOccurredAtUnixMs(int64_t unixMs) { occurredAtUnixMs = unixMs; }

    /**
     * Serializes match data to a byte array
     * @param buffer Buffer to write the serialized data to (must be at least MATCH_BINARY_SIZE bytes)
     * @return Number of bytes written
     */
    size_t serialize(uint8_t* buffer) const;

    /**
     * Deserializes match data from a byte array
     * @param buffer Buffer containing the serialized data
     * @return Number of bytes read
     */
    size_t deserialize(const uint8_t* buffer);

    /**
     * @return Size of serialized match data in bytes
     */
    static size_t binarySize() { return MATCH_BINARY_SIZE; }

    /** @return Null-terminated match UUID string. */
    const char* getMatchId() const { return matchId; }
    /** @return Null-terminated legacy 4-char hunter ID. */
    const char* getHunterId() const { return hunter; }
    /** @return Null-terminated legacy 4-char bounty ID. */
    const char* getBountyId() const { return bounty; }
    /** @return Hunter draw time in milliseconds. */
    unsigned long getHunterDrawTime() const { return hunterDrawTimeMs; }
    /** @return Bounty draw time in milliseconds. */
    unsigned long getBountyDrawTime() const { return bountyDrawTimeMs; }

    /** @return 16-byte match UUID. */
    const UUIDBytes& getMatchUuid() const { return matchUuid; }
    /** @return 16-byte hunter player UUID. */
    const UUIDBytes& getHunterPlayerId() const { return hunterPlayerId; }
    /** @return 16-byte bounty player UUID. */
    const UUIDBytes& getBountyPlayerId() const { return bountyPlayerId; }
    /** @return 16-byte hunter team UUID. */
    const UUIDBytes& getHunterTeamId() const { return hunterTeamId; }
    /** @return 16-byte bounty team UUID. */
    const UUIDBytes& getBountyTeamId() const { return bountyTeamId; }
    /** @return 16-byte winning player UUID. */
    const UUIDBytes& getWinnerPlayerId() const { return winnerPlayerId; }
    /** @return 6-byte hunter device MAC address. */
    const uint8_t* getHunterMac() const { return hunterMac; }
    /** @return 6-byte bounty device MAC address. */
    const uint8_t* getBountyMac() const { return bountyMac; }
    /** @return Timestamp at which the match finalized (milliseconds). */
    int64_t getOccurredAtUnixMs() const { return occurredAtUnixMs; }

private:
    char matchId[IdGenerator::UUID_BUFFER_SIZE] = {};
    char hunter[5] = {};
    char bounty[5] = {};
    unsigned long hunterDrawTimeMs = 0;
    unsigned long bountyDrawTimeMs = 0;
    UUIDBytes matchUuid = {};
    UUIDBytes hunterPlayerId = {};
    UUIDBytes bountyPlayerId = {};
    UUIDBytes hunterTeamId = {};
    UUIDBytes bountyTeamId = {};
    UUIDBytes winnerPlayerId = {};
    uint8_t hunterMac[MAC_BINARY_SIZE] = {};
    uint8_t bountyMac[MAC_BINARY_SIZE] = {};
    int64_t occurredAtUnixMs = 0;
};
