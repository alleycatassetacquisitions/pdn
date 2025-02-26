#pragma once

#include <string>
#include "../../include/id-generator.hpp"

using namespace std;

// Size of match data in bytes: 3 UUIDs (16 bytes each) + winner flag (1 byte) + 2 draw times (8 bytes each)
#define MATCH_BINARY_SIZE (3 * IdGenerator::UUID_BINARY_SIZE + 1 + 2 * sizeof(unsigned long))

// JSON keys for match serialization
#define JSON_KEY_MATCH_ID "match_id"
#define JSON_KEY_HUNTER_ID "hunter"
#define JSON_KEY_BOUNTY_ID "bounty"
#define JSON_KEY_WINNER_IS_HUNTER "winner_is_hunter"
#define JSON_KEY_HUNTER_TIME "hunter_time"
#define JSON_KEY_BOUNTY_TIME "bounty_time"

/**
 * Represents a duel match between a hunter and bounty player.
 * Stores match details and outcome, with serialization support.
 */
class Match {
public:
    /**
     * Default constructor for empty match
     */
    Match() = default;

    /**
     * Creates a new match with the given IDs
     * @param match_id Unique identifier for the match
     * @param hunter_id Hunter player's ID
     * @param bounty_id Bounty player's ID
     */
    Match(string match_id, string hunter_id, string bounty_id);

    /**
     * Legacy method to setup match details after construction
     */
    void setupMatch(string id, string hunter, string bounty);

    /**
     * Sets the winner of the match
     * @param winner_is_hunter true if hunter won, false if bounty won
     */
    void setWinner(bool winner_is_hunter);

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
     * @return Match data as JSON string
     */
    string toJson() const;

    /**
     * Populates match data from JSON string
     * @param json JSON string containing match data
     */
    void fromJson(const string &json);

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
    string getMatchId() const { return match_id; }
    string getHunterId() const { return hunter; }
    string getBountyId() const { return bounty; }
    bool wasHunterWinner() const { return winner_is_hunter; }
    unsigned long getHunterDrawTime() const { return hunter_draw_time_ms; }
    unsigned long getBountyDrawTime() const { return bounty_draw_time_ms; }

private:
    string match_id;
    string hunter;
    string bounty;
    bool winner_is_hunter = false; // Default to false until explicitly set
    unsigned long hunter_draw_time_ms = 0;
    unsigned long bounty_draw_time_ms = 0;
};
