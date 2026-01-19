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
#include <string>
#include "device/drivers/button.hpp"
#include "game/match.hpp"
#include "game/player.hpp"
#include "wireless/quickdraw-wireless-manager.hpp"
#include "device/drivers/storage-interface.hpp"

// Preferences namespace and keys

struct ActiveDuelState {
    bool hasReceivedDrawResult = false;
    bool hasPressedButton = false;
    bool gracePeriodExpiredNoResult = false;
    unsigned long duelLocalStartTime = 0;
    unsigned long BUTTON_MASHER_PENALTY_MS = 75;
    int buttonMasherCount = 0;
    Match* match = nullptr;
};

class MatchManager {
public:
    static MatchManager* GetInstance();
    /**
     * Creates a new active match
     * @param match_id UUID of the match
     * @param hunter_id Hunter's UUID
     * @param bounty_id Bounty's UUID
     * @return Pointer to the newly created match
     */
    Match* createMatch(const std::string& match_id, const std::string& hunter_id, const std::string& bounty_id);

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

    void setDuelLocalStartTime(unsigned long local_start_time_ms);

    void setNeverPressed();

    bool didWin();

    unsigned long getDuelLocalStartTime();

    bool matchResultsAreIn();

    bool getHasReceivedDrawResult();
    bool getHasPressedButton();
    void setReceivedDrawResult();
    void setReceivedButtonPush();

    /**
     * Gets the current active match if any
     * @return Pointer to the active match, nullptr if none
     */
    Match* getCurrentMatch() const { return activeDuelState.match; }

    /**
     * Converts all stored matches to a JSON array string
     * @return JSON string containing all stored matches
     */
    std::string toJson();

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

    void listenForMatchResults(QuickdrawCommand command);

    void initialize(Player* player, StorageInterface* storage, PeerCommsInterface* peerComms);

    parameterizedCallbackFunction getDuelButtonPush();

    parameterizedCallbackFunction getButtonMasher();

protected:
    Player* player;


private:
    MatchManager();
    ~MatchManager();


    ActiveDuelState activeDuelState;

    parameterizedCallbackFunction duelButtonPush;
    parameterizedCallbackFunction buttonMasher;

    StorageInterface* storage;
    PeerCommsInterface* peerComms;
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

};




