#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include "../../include/id-generator.hpp"
#include "utils/uuid-bytes.hpp"

/**
 * A Match as NVS stores it and kMatchCollection frames carry it. Field order is
 * the layout; packed so PDN and FDN builds agree on it.
 */
struct MatchRecord {
    UUIDBytes matchUuid;
    uint32_t hunterDrawTimeMs;
    uint32_t bountyDrawTimeMs;
    UUIDBytes hunterPlayerId;
    UUIDBytes bountyPlayerId;
    UUIDBytes hunterTeamId;
    UUIDBytes bountyTeamId;
    UUIDBytes winnerPlayerId;
    int64_t occurredAtUnixMs;
} __attribute__((packed));

constexpr size_t MATCH_BINARY_SIZE = sizeof(MatchRecord);
static_assert(MATCH_BINARY_SIZE == 112,
              "stored records and kMatchCollection frames share this layout");

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

    /** Creates a match identified by @p matchId. */
    explicit Match(const char* matchId);

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

    /** @return Null-terminated match ID string. */
    const char* getMatchId() const { return matchId; }
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
    /** @return Timestamp at which the match finalized (milliseconds). */
    int64_t getOccurredAtUnixMs() const { return occurredAtUnixMs; }

private:
    char matchId[IdGenerator::UUID_BUFFER_SIZE] = {};
    unsigned long hunterDrawTimeMs = 0;
    unsigned long bountyDrawTimeMs = 0;
    UUIDBytes matchUuid = {};
    UUIDBytes hunterPlayerId = {};
    UUIDBytes bountyPlayerId = {};
    UUIDBytes hunterTeamId = {};
    UUIDBytes bountyTeamId = {};
    UUIDBytes winnerPlayerId = {};
    int64_t occurredAtUnixMs = 0;
};
