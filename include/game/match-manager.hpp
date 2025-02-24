/*
    This class is responsible for managing the matches.
    It will be responsible for creating, updating and deleting matches.
    In addition to creating and updating matches, this class is responsible for
    saving and loading matches from EEPROM. In addition to saving/loading from
    EEPROM, this class will be responsible for serializing and deserializing
    matches to and from a JSON array.
*/
#pragma once

#include <vector>
#include <EEPROM.h>
#include "../lib/pdn-libs/match.hpp"

// EEPROM layout:
// First byte: Number of matches (max 255)
// Following bytes: Match data, each MATCH_BINARY_SIZE bytes long
#define EEPROM_MATCH_COUNT_ADDR 0
#define EEPROM_MATCHES_START_ADDR 1
#define MAX_MATCHES 255

class MatchManager {
public:
    static MatchManager* GetInstance() {
        static MatchManager instance;
        return &instance;
    }

    /**
     * Creates a new active match
     * @param hunter_id Hunter's UUID
     * @param bounty_id Bounty's UUID
     * @return Pointer to the newly created match
     */
    Match* createMatch(const string& hunter_id, const string& bounty_id);

    /**
     * Initializes a match received from another player
     * @param match_json JSON string containing match data
     * @return Pointer to the initialized match, nullptr if invalid
     */
    Match* receiveMatch(const string& match_json);

    /**
     * Finalizes a match by saving it to EEPROM and removing from active matches
     * @param match_id UUID of the match to finalize
     * @return true if match was found and saved
     */
    bool finalizeMatch(const string& match_id);

    /**
     * Updates an existing active match with new data
     * @param match_id UUID of the match to update
     * @param winner_is_hunter true if hunter won
     * @param hunter_time_ms hunter's draw time in ms
     * @param bounty_time_ms bounty's draw time in ms
     * @return true if match was found and updated
     */
    bool updateMatch(const string& match_id, bool winner_is_hunter, 
                    unsigned long hunter_time_ms, unsigned long bounty_time_ms);

    /**
     * Finds an active match by its ID
     * @param match_id UUID of the match to find
     * @return Pointer to the match if found, nullptr otherwise
     */
    Match* findActiveMatch(const string& match_id);

    /**
     * Gets the current active match if any
     * @return Pointer to the active match, nullptr if none
     */
    Match* getCurrentMatch() const { return activeMatch; }

    /**
     * Converts all stored matches from EEPROM to a JSON array string
     * @return JSON string containing all stored matches
     */
    string toJson() const;

    /**
     * Clears all matches from EEPROM storage
     */
    void clearStorage();

    /**
     * Gets the number of stored matches in EEPROM
     * @return Number of matches
     */
    size_t getStoredMatchCount() const;

private:
    MatchManager() : activeMatch(nullptr) {}
    ~MatchManager();

    /**
     * Appends a match to EEPROM storage
     * @param match Match to save
     * @return true if save was successful
     */
    bool appendMatchToEEPROM(const Match* match);

    /**
     * Updates the stored match count in EEPROM
     */
    void updateStoredMatchCount(uint8_t count);

    /**
     * Reads a match from EEPROM at the specified index
     * @param index Index of the match to read
     * @return New Match object if successful, nullptr if invalid
     */
    Match* readMatchFromEEPROM(uint8_t index) const;

    Match* activeMatch;  // Currently active match (only one at a time)

    // Prevent copying of singleton
    MatchManager(const MatchManager&) = delete;
    MatchManager& operator=(const MatchManager&) = delete;
};




