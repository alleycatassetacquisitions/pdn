/*
    This class is responsible for managing the matches.
    It will be responsible for creating, updating and deleting matches.
    In addition to creating and updating matches, this class is responsible for
    saving and loading matches from EEPROM. In addition to saving/loading from
    EEPROM, this class will be responsible for serializing and deserializing
    matches to and from a JSON array.
*/
#pragma once

#include <array>
#include <functional>
#include <optional>
#include <vector>
#include <string>
#include "device/drivers/button.hpp"
#include "device/remote-device-coordinator.hpp"
#include "game/match.hpp"
#include "game/player.hpp"
#include "wireless/quickdraw-wireless-manager.hpp"
#include "device/drivers/storage-interface.hpp"

// Preferences namespace and keys

struct LastMatchDisplay {
    unsigned long myTimeMs = 0;       // boosted draw time, as used for winner calc
    unsigned long opponentTimeMs = 0;
    unsigned long boostMs = 0;        // boost applied at own button press
    bool hasData = false;
};

struct ActiveDuelState {
    bool matchIsReady = false;
    bool hasReceivedDrawResult = false;
    bool hasPressedButton = false;
    bool gracePeriodExpiredNoResult = false;
    bool opponentNeverPressed = false;
    unsigned long duelLocalStartTime = 0;
    unsigned long BUTTON_MASHER_PENALTY_MS = 75;
    int buttonMasherCount = 0;
    std::optional<Match> match;
    std::array<uint8_t, 6> opponentMac = {};

    unsigned long calculateButtonMasherPenalty() {
        return BUTTON_MASHER_PENALTY_MS * buttonMasherCount;
    };
};

class MatchManager {
public:

    MatchManager();
    ~MatchManager();
    

    /*
     * Initializes a match and initiates ESP-NOW messaging to opponent.
    */
    void initializeMatch(uint8_t* opponentMac);

    /**
     * Initializes a match received from another player
     * @param match_json JSON string containing match data
     * @return Pointer to the initialized match, nullptr if invalid
     */
    void receiveMatch(const char* matchId, const char* opponentId, bool isHunter, uint8_t* opponentMac);

    bool isMatchReady();

    /**
     * Finalizes a match by saving it to storage and removing from active matches
     * @return true if match was found and saved
     */
    bool finalizeMatch();

    bool setHunterDrawTime(unsigned long hunter_time_ms);
    bool setBountyDrawTime(unsigned long bounty_time_ms);

    void setDuelLocalStartTime(unsigned long local_start_time_ms);

    bool didWin();

    unsigned long getDuelLocalStartTime();

    bool matchResultsAreIn();

    bool getHasReceivedDrawResult();
    bool getHasPressedButton();

    /**
     * Gets the current active match if any
     * @return Reference to the optional match
     */
    std::optional<Match>& getCurrentMatch() { return activeDuelState.match; }

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

    void setBoostProvider(std::function<unsigned long()> provider);

    const LastMatchDisplay& getLastMatchDisplay() const { return lastMatchDisplay_; }

    // Required for SEND_MATCH_ID to be accepted: sender MAC must match one
    // of the RDC's direct-peer MACs (cable-established neighbor). If unset,
    // SEND_MATCH_ID is refused — no match initiation from an unknown MAC.
    void setRemoteDeviceCoordinator(RemoteDeviceCoordinator* rdc);

    void listenForMatchEvents(const QuickdrawCommand& command);

    void initialize(Player* player, StorageInterface* storage, QuickdrawWirelessManager* quickdrawWirelessManager);

    parameterizedCallbackFunction getDuelButtonPush();

    parameterizedCallbackFunction getButtonMasher();

    void sendNeverPressed(unsigned long pityTime);

    // For testing purposes only DO NOT USE IN PRODUCTION
    void setReceivedDrawResult();
    void setReceivedButtonPush();
    void setNeverPressed();
    void setOpponentNeverPressed() { activeDuelState.opponentNeverPressed = true; }


private:

    Player* player;

    std::function<unsigned long()> boostProvider_;
    RemoteDeviceCoordinator* rdc_ = nullptr;

    ActiveDuelState activeDuelState;
    LastMatchDisplay lastMatchDisplay_;

    parameterizedCallbackFunction duelButtonPush;
    parameterizedCallbackFunction buttonMasher;

    StorageInterface* storage;
    QuickdrawWirelessManager* quickdrawWirelessManager;
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

    // Returns true when `command` belongs to the currently-active match and
    // was sent by that match's opponent. All post-handshake duel commands
    // (MATCH_ID_ACK, MATCH_ROLE_MISMATCH, DRAW_RESULT, NEVER_PRESSED) must
    // pass this gate before mutating match state — otherwise any neighbor
    // in ESP-NOW range could forge a result for a match they're not in.
    // SEND_MATCH_ID is intentionally exempt: it's the packet that
    // establishes the peering in the first place.
    bool isFromActiveMatchOpponent(const QuickdrawCommand& command) const;

    void sendMatchAck();
    void sendMatchId();
    void sendMatchRoleMismatch(const QuickdrawCommand& incoming);
};




