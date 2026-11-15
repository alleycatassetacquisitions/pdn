#pragma once

#include <cstddef>
#include <cstdint>
#include "../../include/id-generator.hpp"
#include "utils/uuid-bytes.hpp"

/**
 * A Match as NVS stores it. Packed: field order is the 112-byte layout.
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
static_assert(MATCH_BINARY_SIZE == 112, "stored records use this layout");

/**
 * A match between a hunter and a bounty: its text id for the handshake and the
 * record it is stored as.
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
     * Sets the hunter's raw draw time
     * @param timeMs draw time in milliseconds
     */
    void setHunterDrawTime(unsigned long timeMs) {
        record.hunterDrawTimeMs = static_cast<uint32_t>(timeMs);
    }

    /**
     * Sets the bounty's raw draw time
     * @param timeMs draw time in milliseconds
     */
    void setBountyDrawTime(unsigned long timeMs) {
        record.bountyDrawTimeMs = static_cast<uint32_t>(timeMs);
    }

    /** Names the player and team UUIDs of the hunter slot, or of the bounty slot. */
    void setPlayer(bool hunterSlot, const UUIDBytes& playerId, const UUIDBytes& teamId) {
        (hunterSlot ? record.hunterPlayerId : record.bountyPlayerId) = playerId;
        (hunterSlot ? record.hunterTeamId : record.bountyTeamId) = teamId;
    }

    /** Sets the 16-byte winning player UUID. */
    void setWinnerPlayerId(const UUIDBytes& id) { record.winnerPlayerId = id; }

    /** Sets the UTC time at which the match finalized (milliseconds). */
    void setOccurredAtUnixMs(int64_t unixMs) { record.occurredAtUnixMs = unixMs; }

    /**
     * Writes the record to @p buffer (at least MATCH_BINARY_SIZE bytes).
     * @return Number of bytes written
     */
    size_t serialize(uint8_t* buffer) const;

    /**
     * Reads the record from @p buffer; the match id becomes the record's UUID as text.
     * @return Number of bytes read
     */
    size_t deserialize(const uint8_t* buffer);

    /** @return Null-terminated match ID string. */
    const char* getMatchId() const { return matchId; }
    /** @return Hunter raw draw time in milliseconds. */
    unsigned long getHunterDrawTime() const { return record.hunterDrawTimeMs; }
    /** @return Bounty raw draw time in milliseconds. */
    unsigned long getBountyDrawTime() const { return record.bountyDrawTimeMs; }
    /** @return 16-byte hunter player UUID. */
    const UUIDBytes& getHunterPlayerId() const { return record.hunterPlayerId; }
    /** @return 16-byte bounty player UUID. */
    const UUIDBytes& getBountyPlayerId() const { return record.bountyPlayerId; }
    /** @return 16-byte hunter team UUID. */
    const UUIDBytes& getHunterTeamId() const { return record.hunterTeamId; }
    /** @return 16-byte bounty team UUID. */
    const UUIDBytes& getBountyTeamId() const { return record.bountyTeamId; }
    /** @return 16-byte winning player UUID. */
    const UUIDBytes& getWinnerPlayerId() const { return record.winnerPlayerId; }
    /** @return UTC time at which the match finalized (milliseconds). */
    int64_t getOccurredAtUnixMs() const { return record.occurredAtUnixMs; }

private:
    char matchId[IdGenerator::UUID_BUFFER_SIZE] = {};
    MatchRecord record = {};
};
