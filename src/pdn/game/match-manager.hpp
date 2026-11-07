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
#include "wireless/mac-functions.hpp"
#include "wireless/match-packet.hpp"
#include "wireless/reliable-channel.hpp"
#include "wireless/resender.hpp"
#include "device/drivers/storage-interface.hpp"

class ShootoutManager;

// Preferences namespace and keys
inline constexpr const char MATCHES_PREFS_NAMESPACE[] = "matches";

struct LastMatchDisplay {
    unsigned long myTimeMs = 0;       // boosted draw time, as used for winner calc
    unsigned long opponentTimeMs = 0;
    unsigned long boostMs = 0;        // boost applied at own button press
    bool hasData = false;
};

struct ActiveMatchState {
    bool matchIsReady = false;
    // The draw slot for THIS match, which a shootout assigns from MAC
    // ordering rather than from the player's standing role.
    bool localIsHunter = false;
    bool hasReceivedDrawResult = false;
    bool hasPressedButton = false;
    bool gracePeriodExpiredNoResult = false;
    bool opponentNeverPressed = false;
    unsigned long matchLocalStartTime = 0;
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
    /** Claims the kMatchCommand channel on wirelessManager (may be nullptr in tests). */
    explicit MatchManager(WirelessManager* wirelessManager);
    ~MatchManager();

    /** Initializes a match and sends SEND_MATCH_ID to opponentMac. */
    void initializeMatch(uint8_t* opponentMac);

    /** Primes a match from an incoming SEND_MATCH_ID and replies with MATCH_ID_ACK. */
    void receiveMatch(const char* matchId, const char* opponentId, bool opponentIsHunter,
                      uint8_t* opponentMac);

    /**
     * Shootout mode: primes a match without the SEND_MATCH_ID handshake. Both
     * players call this independently on MATCH_START with the same derived
     * match ID and opposite `localIsHunter`, so each writes exactly one of the
     * match's two draw-time slots.
     */
    void initializeShootoutMatch(const char* matchId, uint8_t* opponentMac, bool localIsHunter);

    /** True once the match handshake has completed and countdown may begin. */
    bool isMatchReady() const;

    /** Finalizes the active match (persisting non-shootout matches to storage) and clears active
     * match state. */
    bool finalizeMatch();

    bool setHunterDrawTime(unsigned long hunter_time_ms);
    bool setBountyDrawTime(unsigned long bounty_time_ms);

    /** Records the local timestamp when the match draw window starts. */
    void setMatchLocalStartTime(unsigned long localStartTimeMs);

    bool didWin();

    /**
     * The draw slot this device fills for the active match. primeMatch is the
     * only writer and sets it alongside the match; the whole-struct reset clears
     * both, so there is no slot without a match behind it.
     */
    bool isLocalHunter() const { return activeMatchState.localIsHunter; }

    /** Timestamp when the match draw window started. */
    unsigned long getMatchLocalStartTime() const;

    bool matchResultsAreIn();

    /** True once the opponent's draw result has arrived for the active match. */
    bool getHasReceivedDrawResult() const;

    /** True once the local player has pressed the draw button for the active match. */
    bool getHasPressedButton() const;

    /** Returns the active match, or std::nullopt if none. */
    std::optional<Match>& getCurrentMatch() { return activeMatchState.match; }

    /** Serializes all stored matches to a JSON array string. */
    std::string toJson();

    /** Clears all matches from storage. */
    void clearStorage();

    /** Returns the number of stored matches. */
    size_t getStoredMatchCount();

    /** Clears the active match and resets all per-match state flags. */
    void clearCurrentMatch();

    /**
     * Clears the active match only when a shootout primed it, leaving a match the
     * cable handshake owns untouched.
     */
    void clearShootoutMatch();

    void setBoostProvider(std::function<unsigned long()> provider);

    /**
     * Result-screen snapshot that outlives the match: draw times from
     * finalizeMatch, boost from the button press that earned it.
     */
    const LastMatchDisplay& getLastMatchDisplay() const { return lastMatchDisplay; }

    // Required for SEND_MATCH_ID to be accepted: sender MAC must match one
    // of the RDC's direct-peer MACs (cable-established neighbor). If unset,
    // SEND_MATCH_ID is refused — no match initiation from an unknown MAC.
    void setRemoteDeviceCoordinator(RemoteDeviceCoordinator* rdc);

    // Shootout tournament suppresses the hunter/bounty role-mismatch rejection
    // so same-role matches in the bracket proceed. Injected by GameSession after
    // both managers are built.
    void setShootoutManager(ShootoutManager* shootoutManager);
    ShootoutManager* getShootoutManager() const;

    /** Dispatches an inbound match command (handshake or active-match event). */
    void listenForMatchEvents(const MatchCommand& command);

    /** Binds the player and storage this manager records matches against. */
    void initialize(Player* player, StorageInterface* storage);

    /** Drives the match channel's retries. */
    void sync();

    /** Button callback that records a valid match draw press and sends DRAW_RESULT. */
    parameterizedCallbackFunction getMatchButtonPush();

    parameterizedCallbackFunction getButtonMasher();

    void sendNeverPressed(unsigned long pityTime);

    // For testing purposes only DO NOT USE IN PRODUCTION
    void setReceivedDrawResult();
    void setReceivedButtonPush();
    void setNeverPressed();
    /** Test-only helper that marks the opponent as never having pressed. */
    void setOpponentNeverPressed() { activeMatchState.opponentNeverPressed = true; }

private:
    /** True when the active match carries a shootout-derived match id. */
    bool currentMatchIsShootout() const;

    Player* player;

    std::function<unsigned long()> boostProvider_;
    RemoteDeviceCoordinator* rdc_ = nullptr;
    ShootoutManager* shootoutManager_ = nullptr;

    ActiveMatchState activeMatchState;
    LastMatchDisplay lastMatchDisplay;

    parameterizedCallbackFunction matchButtonPush;
    parameterizedCallbackFunction buttonMasher;

    StorageInterface* storage;
    Resender resender;
    ReliableChannel<MatchPacket> matchChannel;
    void sendCommand(const uint8_t* mac, int command, const char* matchId,
                     long drawTime, bool isHunter);
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
    // was sent by that match's opponent. All post-handshake match commands
    // (MATCH_ID_ACK, MATCH_ROLE_MISMATCH, DRAW_RESULT, NEVER_PRESSED) must
    // pass this gate before mutating match state — otherwise any neighbor
    // in ESP-NOW range could forge a result for a match they're not in.
    // SEND_MATCH_ID is intentionally exempt: it's the packet that
    // establishes the peering in the first place.
    bool isFromActiveMatchOpponent(const MatchCommand& command) const;

    // Emplace match with given id/opponent/draw slot. Common path for both the
    // cable-handshake init and the Shootout MATCH_START init.
    void primeMatch(const char* matchId, const uint8_t* opponentMac, bool localIsHunter);
};




