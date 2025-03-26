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
#include <Preferences.h>
#include "../lib/pdn-libs/match.hpp"

// Preferences namespace and keys
#define PREF_NAMESPACE "matches"
#define PREF_COUNT_KEY "count"
#define PREF_MATCH_KEY "match_"  // Will be appended with index
#define MAX_MATCHES 255

class MatchManager {
public:
    static MatchManager* GetInstance() {
        static MatchManager instance;
        return &instance;
    }

    /**
     * Creates a new active match
     * @param match_id UUID of the match
     * @param hunter_id Hunter's UUID
     * @param bounty_id Bounty's UUID
     * @return Pointer to the newly created match
     */
    Match* createMatch(const string& match_id, const string& hunter_id, const string& bounty_id);

    /**
     * Initializes a match received from another player
     * @param match_json JSON string containing match data
     * @return Pointer to the initialized match, nullptr if invalid
     */
    Match* receiveMatch(Match match);

    /**
     * Finalizes a match by saving it to storage and removing from active matches
     * @param match_id UUID of the match to finalize
     * @return true if match was found and saved
     */
    bool finalizeMatch();

    bool setHunterDrawTime(unsigned long hunter_time_ms);
    bool setBountyDrawTime(unsigned long bounty_time_ms);

    bool matchIsFinalized(bool isHunter);

    /**
     * Gets the current active match if any
     * @return Pointer to the active match, nullptr if none
     */
    Match* getCurrentMatch() const { return activeMatch; }

    /**
     * Converts all stored matches to a JSON array string
     * @return JSON string containing all stored matches
     */
    string toJson();

    /**
     * Clears all matches from storage
     */
    void clearStorage();

    /**
     * Gets the number of stored matches
     * @return Number of matches
     */
    size_t getStoredMatchCount();

    void clearCurrentMatch();

private:
    MatchManager();
    ~MatchManager();

    /**
     * Appends a match to storage
     * @param match Match to save
     * @return true if save was successful
     */
    bool appendMatchToStorage(const Match* match);

    /**
     * Updates the stored match count
     */
    void updateStoredMatchCount(uint8_t count);

    /**
     * Reads a match from storage at the specified index
     * @param index Index of the match to read
     * @return New Match object if successful, nullptr if invalid
     */
    Match* readMatchFromStorage(uint8_t index);

    Match* activeMatch;  // Currently active match (only one at a time)
    Preferences prefs;   // Preferences instance for persistent storage

    // Prevent copying of singleton
    MatchManager(const MatchManager&) = delete;
    MatchManager& operator=(const MatchManager&) = delete;
};




