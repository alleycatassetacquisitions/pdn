#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include "../../include/id-generator.hpp"
#include "utils/uuid-bytes.hpp"

// Player ID is stored as 4 raw bytes in the legacy short-ID slot
constexpr size_t PLAYER_ID_BINARY_SIZE = 4;
constexpr size_t MAC_BINARY_SIZE = 6;
constexpr long MIN_DRAW_TIME_MS = -300;

// Binary layout:
// - matchUuid (16B)
// - legacy hunter/bounty short IDs (2 * 4B)
// - signed hunter/bounty draw times (2 * sizeof(long))
// - hunterPlayerId, bountyPlayerId, hunterTeamId, bountyTeamId, winnerPlayerId (5 * 16B)
// - hunterMac, bountyMac (2 * 6B)
// - occurredAtUnixMs (8B)
constexpr size_t MATCH_BINARY_SIZE = IdGenerator::UUID_BINARY_SIZE + 2 * PLAYER_ID_BINARY_SIZE +
                                     2 * sizeof(long) + 5 * UUIDBytes::SIZE + 2 * MAC_BINARY_SIZE +
                                     sizeof(int64_t);

// JSON keys for match serialization
constexpr const char* JSON_KEY_MATCH_ID = "match_id";
constexpr const char* JSON_KEY_HUNTER_ID = "hunter";
constexpr const char* JSON_KEY_BOUNTY_ID = "bounty";
constexpr const char* JSON_KEY_WINNER_IS_HUNTER = "winner_is_hunter";
constexpr const char* JSON_KEY_HUNTER_TIME = "hunter_time";
constexpr const char* JSON_KEY_BOUNTY_TIME = "bounty_time";

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
     * Sets the hunter's draw time (clamped to >= MIN_DRAW_TIME_MS).
     * @param timeMs draw time in milliseconds
     */
    void setHunterDrawTime(long timeMs);

    /**
     * Sets the bounty's draw time (clamped to >= MIN_DRAW_TIME_MS).
     * @param timeMs draw time in milliseconds
     */
    void setBountyDrawTime(long timeMs);

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

    /** Sets the 16-byte match UUID and formats its ASCII representation. */
    void setMatchUuid(const UUIDBytes& uuid);
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
     * @return Match data as JSON string
     */
    std::string toJson() const;

    /**
     * Populates match data from JSON string
     * @param json JSON string containing match data
     */
    void fromJson(const std::string& json);

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
    const char* getMatchId() const { return match_id; }
    /** @return Null-terminated legacy 4-char hunter ID. */
    const char* getHunterId() const { return hunter; }
    /** @return Null-terminated legacy 4-char bounty ID. */
    const char* getBountyId() const { return bounty; }
    /** @return Signed hunter draw time in milliseconds (>= MIN_DRAW_TIME_MS). */
    long getHunterDrawTime() const { return hunterDrawTimeMs; }
    /** @return Signed bounty draw time in milliseconds (>= MIN_DRAW_TIME_MS). */
    long getBountyDrawTime() const { return bountyDrawTimeMs; }

    /** @return Hunter draw time clamped to >= 0 for unsigned server upload. */
    uint64_t getClampedHunterDrawTimeMs() const {
        return hunterDrawTimeMs < 0 ? 0ULL : static_cast<uint64_t>(hunterDrawTimeMs);
    }

    /** @return Bounty draw time clamped to >= 0 for unsigned server upload. */
    uint64_t getClampedBountyDrawTimeMs() const {
        return bountyDrawTimeMs < 0 ? 0ULL : static_cast<uint64_t>(bountyDrawTimeMs);
    }

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
    char match_id[IdGenerator::UUID_BUFFER_SIZE] = {};
    char hunter[5] = {};
    char bounty[5] = {};
    long hunterDrawTimeMs = 0;
    long bountyDrawTimeMs = 0;
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
