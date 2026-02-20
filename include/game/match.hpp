#pragma once

#include <string>
#include "../../include/id-generator.hpp"

// Size of match data in bytes: 3 UUIDs (16 bytes each) + 2 draw times (8 bytes each)
constexpr size_t MATCH_BINARY_SIZE = 3 * IdGenerator::UUID_BINARY_SIZE + 2 * sizeof(unsigned long);

// JSON keys for match serialization
constexpr const char* JSON_KEY_MATCH_ID = "match_id";
constexpr const char* JSON_KEY_HUNTER_ID = "hunter";
constexpr const char* JSON_KEY_BOUNTY_ID = "bounty";
constexpr const char* JSON_KEY_WINNER_IS_HUNTER = "winner_is_hunter";
constexpr const char* JSON_KEY_HUNTER_TIME = "hunter_time";
constexpr const char* JSON_KEY_BOUNTY_TIME = "bounty_time";

/**
 * Represents a duel match between a hunter and bounty player.
 * Stores match details and outcome, with serialization support.
 */
class Match {
public:
    /**
     * Default constructor for empty match
     */
    Match();

    /**
     * Creates a new match with the given IDs (std::string overload)
     * @param match_id Unique identifier for the match
     * @param hunter_id Hunter player's ID
     * @param bounty_id Bounty player's ID
     */
    Match(const std::string& match_id, const std::string& hunter_id, const std::string& bounty_id);

    /**
     * Creates a new match from raw C-strings — no heap allocation in the hot path.
     */
    Match(const char* match_id, const char* hunter_id, const char* bounty_id);

    /**
     * Fast constructor for the packet-receive hot path.
     * Uses memcpy instead of strncpy.
     * Precondition: match_id is exactly UUID_STRING_LENGTH chars, hunter_id and bounty_id
     * are exactly 4 chars. Behaviour is undefined if these lengths are not met.
     */
    Match(const char* match_id, const char* hunter_id, const char* bounty_id,
          unsigned long hunter_time, unsigned long bounty_time);

    /**
     * Legacy method to setup match details after construction
     */
    void setupMatch(const std::string& id, const std::string& hunter, const std::string& bounty);

    /**
     * Sets the hunter's draw time
     * @param timeMs draw time in milliseconds
     */
    void setHunterDrawTime(unsigned long timeMs);

    /**
     * Sets the bounty's draw time
     * @param timeMs draw time in milliseconds
     */
    void setBountyDrawTime(unsigned long timeMs);

    /**
     * Sets the hunter's ID
     * @param hunter_id Hunter player's ID
     */
    void setHunterId(const char* hunter_id);

    /**
     * @return Match data as JSON string
     */
    std::string toJson() const;

    /**
     * Populates match data from JSON string
     * @param json JSON string containing match data
     */
    void fromJson(const std::string &json);

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

    // Getters — return direct pointers into the fixed-size storage, no heap involved
    const char* getMatchId() const { return match_id; }
    const char* getHunterId() const { return hunter; }
    const char* getBountyId() const { return bounty; }
    unsigned long getHunterDrawTime() const { return hunter_draw_time_ms; }
    unsigned long getBountyDrawTime() const { return bounty_draw_time_ms; }

private:
    char match_id[IdGenerator::UUID_BUFFER_SIZE] = {};
    char hunter[5] = {};
    char bounty[5] = {};
    unsigned long hunter_draw_time_ms = 0;
    unsigned long bounty_draw_time_ms = 0;
};
