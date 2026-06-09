#pragma once

#include <string>
#include "../../include/id-generator.hpp"

constexpr const char* JSON_KEY_MATCH_ID = "match_id";
constexpr const char* JSON_KEY_HUNTER_ID = "hunter";
constexpr const char* JSON_KEY_BOUNTY_ID = "bounty";
constexpr const char* JSON_KEY_WINNER_IS_HUNTER = "winner_is_hunter";
constexpr const char* JSON_KEY_HUNTER_TIME = "hunter_time";
constexpr const char* JSON_KEY_BOUNTY_TIME = "bounty_time";
constexpr const char* JSON_KEY_VOIDED = "voided";

/**
 * Represents a duel match between a hunter and bounty player.
 * Stores match details and outcome, with serialization support.
 */
class Match {
public:
    Match();

    /**
     * Creates a new match from raw C-strings — no heap allocation in the hot path.
     */
    Match(const char* match_id, const char* player_id, bool isHunter);

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
     * Sets the bounty's ID
     * @param bounty_id Bounty player's ID
     */
    void setBountyId(const char* bounty_id);

    /**
     * @return Match data as JSON string
     */
    std::string toJson() const;

    /**
     * Populates match data from JSON string
     * @param json JSON string containing match data
     */
    void fromJson(const std::string &json);

    // Getters — return direct pointers into the fixed-size storage, no heap involved
    const char* getMatchId() const { return match_id; }
    const char* getHunterId() const { return hunter; }
    const char* getBountyId() const { return bounty; }
    unsigned long getHunterDrawTime() const { return hunter_draw_time_ms; }
    unsigned long getBountyDrawTime() const { return bounty_draw_time_ms; }
    bool isVoided() const { return voided; }
    void setVoided(bool v) { voided = v; }

private:
    char match_id[IdGenerator::UUID_BUFFER_SIZE] = {};
    char hunter[5] = {};
    char bounty[5] = {};
    unsigned long hunter_draw_time_ms = 0;
    unsigned long bounty_draw_time_ms = 0;
    bool voided = false;
};
