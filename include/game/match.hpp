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
     * Creates a new match with the given IDs
     * @param match_id Unique identifier for the match
     * @param hunter_id Hunter player's ID
     * @param bounty_id Bounty player's ID
     */
    Match(const std::string& match_id, const std::string& hunter_id, const std::string& bounty_id);

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
    void setHunterId(const std::string& hunter_id);

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

    // Getters
    std::string getMatchId() const { return match_id; }
    std::string getHunterId() const { return hunter; }
    std::string getBountyId() const { return bounty; }
    unsigned long getHunterDrawTime() const { return hunter_draw_time_ms; }
    unsigned long getBountyDrawTime() const { return bounty_draw_time_ms; }

private:
    std::string match_id;
    std::string hunter;
    std::string bounty;
    unsigned long hunter_draw_time_ms = 0;
    unsigned long bounty_draw_time_ms = 0;
};
