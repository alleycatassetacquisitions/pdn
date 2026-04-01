// /*
//     This class is responsible for managing the matches.
//     It will be responsible for creating, updating and deleting matches.
//     In addition to creating and updating matches, this class is responsible for
//     saving and loading matches from EEPROM. In addition to saving/loading from
//     EEPROM, this class will be responsible for serializing and deserializing
//     matches to and from a JSON array.
// */
// #pragma once

// #include <array>
// #include <optional>
// #include <vector>
// #include <string>
// #include "device/drivers/button.hpp"
// #include "game/match.hpp"
// #include "game/player.hpp"
// #include "wireless/quickdraw-wireless-manager.hpp"
// #include "device/drivers/storage-interface.hpp"

// // Preferences namespace and keys

// struct ActiveDuelState {
//     bool matchIsReady = false;
//     bool hasReceivedDrawResult = false;
//     bool hasPressedButton = false;
//     bool gracePeriodExpiredNoResult = false;
//     unsigned long duelLocalStartTime = 0;
//     unsigned long BUTTON_MASHER_PENALTY_MS = 75;
//     int buttonMasherCount = 0;
//     std::optional<Match> match;
//     std::array<uint8_t, 6> opponentMac = {};

//     unsigned long calculateButtonMasherPenalty() {
//         return BUTTON_MASHER_PENALTY_MS * buttonMasherCount;
//     };
// };

// class MatchManager {
// public:

//     MatchManager();
//     ~MatchManager();
    

//     /*
//      * Initializes a match and initiates ESP-NOW messaging to opponent.
//     */
//     void initializeMatch(uint8_t* opponentMac);

//     /**
//      * Initializes a match received from another player
//      * @param match_json JSON string containing match data
//      * @return Pointer to the initialized match, nullptr if invalid
//      */
//     void receiveMatch(const char* matchId, const char* opponentId, bool isHunter, uint8_t* opponentMac);

//     bool isMatchReady();

//     /**
//      * Finalizes a match by saving it to storage and removing from active matches
//      * @return true if match was found and saved
//      */
//     bool finalizeMatch();

//     bool setHunterDrawTime(unsigned long hunter_time_ms);
//     bool setBountyDrawTime(unsigned long bounty_time_ms);

//     void setDuelLocalStartTime(unsigned long local_start_time_ms);

//     bool didWin();

//     unsigned long getDuelLocalStartTime();

//     bool matchResultsAreIn();

//     bool getHasReceivedDrawResult();
//     bool getHasPressedButton();

//     /**
//      * Gets the current active match if any
//      * @return Reference to the optional match
//      */
//     std::optional<Match>& getCurrentMatch() { return activeDuelState.match; }

//     /**
//      * Converts all stored matches to a JSON array string
//      * @return JSON string containing all stored matches
//      */
//     std::string toJson();

//     /**
//      * Clears all matches from storage
//      */
//     void clearStorage();

//     /**
//      * Gets the number of stored matches
//      * @return Number of matches
//      */
//     size_t getStoredMatchCount();

//     void clearCurrentMatch();

//     void listenForMatchEvents(const QuickdrawCommand& command);

//     void initialize(Player* player, StorageInterface* storage, QuickdrawWirelessManager* quickdrawWirelessManager);

//     parameterizedCallbackFunction getDuelButtonPush();

//     parameterizedCallbackFunction getButtonMasher();

//     void sendNeverPressed(unsigned long pityTime);

//     // For testing purposes only DO NOT USE IN PRODUCTION
//     void setReceivedDrawResult();
//     void setReceivedButtonPush();
//     void setNeverPressed();


// private:

//     Player* player;

//     ActiveDuelState activeDuelState;

//     parameterizedCallbackFunction duelButtonPush;
//     parameterizedCallbackFunction buttonMasher;

//     StorageInterface* storage;
//     QuickdrawWirelessManager* quickdrawWirelessManager;
//     /**
//      * Appends a match to storage
//      * @param match Match to save
//      * @return true if save was successful
//      */
//     bool appendMatchToStorage(const Match* match);

//     /**
//      * Updates the stored match count
//      */
//     void updateStoredMatchCount(uint8_t count);

//     /**
//      * Reads a match from storage at the specified index
//      * @param index Index of the match to read
//      * @return New Match object if successful, nullptr if invalid
//      */
//     Match* readMatchFromStorage(uint8_t index);

//     void sendMatchAck();
//     void sendMatchId();
//     void sendMatchRoleMismatch();
// };




