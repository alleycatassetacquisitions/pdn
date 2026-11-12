/*
    This class is responsible for managing the matches.
    It will be responsible for creating, updating and deleting matches.
    In addition to creating and updating matches, this class is responsible for
    saving and loading matches from NVS.
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
#include "device/drivers/storage-interface.hpp"

class ShootoutManager;

inline constexpr const char MATCHES_PREFS_NAMESPACE[] = "matches";
inline constexpr const char PREF_SEQ_KEY[] = "seq";
inline constexpr const char PREF_COLLECTED_SEQ_KEY[] = "collectedSeq";
inline constexpr const char PREF_MATCH_KEY_PREFIX[] = "match_";
inline constexpr uint8_t MATCH_RING_CAPACITY = 32;
inline constexpr unsigned long MATCH_TRANSFER_WINDOW_MS = 100;

struct LastMatchDisplay {
    // Effective draw times, as used for winner calc: raw minus boost, so a big
    // enough posse behind a fast draw puts them below zero.
    long myTimeMs = 0;
    long opponentTimeMs = 0;
    unsigned long boostMs = 0;        // boost applied at own button press
    unsigned long penaltyMs = 0;      // early-press penalty applied at own button press
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
    // Bounty: seqId of the MATCH_ID_ACK whose delivery report is awaited (0 = none).
    uint8_t pendingAckSeqId = 0;
    // Earned at the local button press: taken off the raw time when the two
    // draws are compared, and shown on the result screen.
    unsigned long boostMs = 0;
    unsigned long penaltyMs = 0;
    // The boost the opponent's draw result carried.
    unsigned long opponentBoostMs = 0;
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
    /**
     * Claims the kMatchCommand channel and the kMatchCollectionAck handler on
     * wirelessManager. wirelessManager and rdc may be null in tests; with a null
     * rdc every SEND_MATCH_ID is refused.
     */
    MatchManager(Player* player, StorageInterface* storage, WirelessManager* wirelessManager,
                 RemoteDeviceCoordinator* rdc);
    ~MatchManager();

    /** Initializes a match and sends SEND_MATCH_ID to opponentMac. */
    void initializeMatch(uint8_t* opponentMac);

    /** Primes a match from an incoming SEND_MATCH_ID and replies with MATCH_ID_ACK. */
    void receiveMatch(const char* matchId, bool opponentIsHunter, uint8_t* opponentMac,
                      const UUIDBytes& opponentPlayerUuid = UUIDBytes{},
                      const UUIDBytes& opponentTeamUuid = UUIDBytes{});

    /**
     * Shootout mode: primes a match without the SEND_MATCH_ID handshake. Both
     * players call this independently on MATCH_START with the same derived
     * match ID and opposite `localIsHunter`, so each writes exactly one of the
     * match's two draw-time slots.
     */
    void initializeShootoutMatch(const char* matchId, uint8_t* opponentMac, bool localIsHunter);

    /** True once the match handshake has completed and countdown may begin. */
    bool isMatchReady() const;

    /** Finalizes the active match and clears it unless the storage write fails. Shootout,
     * demo, tied and half-reported matches are not stored. */
    bool finalizeMatch();

    /** Sets the hunter draw time (ms) on the active match. */
    bool setHunterDrawTime(unsigned long hunterTimeMs);
    /** Sets the bounty draw time (ms) on the active match. */
    bool setBountyDrawTime(unsigned long bountyTimeMs);

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

    /** Returns the sequence number of the newest stored match (0 when none stored). */
    uint32_t getMatchSequenceNumber();

    /** Returns the highest match sequence number an FDN acked or the PDN skipped as unreadable. */
    uint32_t getCollectedHighWaterMark();

    /** Reads the match for 1-based sequence number `seq` if still in the 32-slot ring buffer. */
    bool readMatchBySequence(uint32_t seq, Match& out);

    /** Clears the active match and resets all per-match state flags. */
    void clearCurrentMatch();

    /**
     * Clears the active match only when a shootout primed it, leaving a match the
     * cable handshake owns untouched.
     */
    void clearShootoutMatch();

    void setBoostProvider(std::function<unsigned long()> provider);

    /**
     * Result-screen snapshot that outlives the match, taken whole by finalizeMatch.
     */
    const LastMatchDisplay& getLastMatchDisplay() const { return lastMatchDisplay; }

    /**
     * Called by ShootoutManager as it is built on this manager and again, with
     * nullptr, as it is destroyed. A live tournament suppresses the
     * hunter/bounty role-mismatch rejection so same-role bracket matches proceed.
     */
    void setShootoutManager(ShootoutManager* shootoutManager);

    /** Dispatches an inbound match command (handshake or active-match event). */
    void listenForMatchEvents(const MatchCommand& command);

    /** Sends stored matches to a cabled FDN; call every tick. */
    void sync();
    /** Button callback that records a valid match draw press and sends DRAW_RESULT. */
    parameterizedCallbackFunction getMatchButtonPush();

    parameterizedCallbackFunction getButtonMasher();

    /** Resets the early-press penalty counter at the start of a new countdown. */
    void clearButtonMasherCount();

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

    std::function<unsigned long()> boostProvider;
    RemoteDeviceCoordinator* rdc = nullptr;
    ShootoutManager* shootoutManager_ = nullptr;

    ActiveMatchState activeMatchState;
    LastMatchDisplay lastMatchDisplay;

    parameterizedCallbackFunction matchButtonPush;
    parameterizedCallbackFunction buttonMasher;

    StorageInterface* storage;
    WirelessManager* wirelessManager = nullptr;
    ReliableChannel<MatchPacket> matchChannel;
    SimpleTimer transferWindowTimer;
    // Sequence of the record sent and not yet acked; 0 when none is outstanding.
    uint32_t sentRecordSeq = 0;

    void initialize();
    /** @return the seqId the frame went out under. */
    uint8_t sendCommand(const uint8_t* mac, int command, const char* matchId, long drawTime,
                        unsigned long boostMs, bool isHunter);
    // The slot's raw draw time minus its player's posse boost; below zero when
    // the boost is the larger. Needs an active match.
    long getEffectiveDrawTime(bool hunterSlot) const;
    /** Appends a match to the binary ring buffer and increments the sequence number. */
    bool appendMatchToStorage(const Match* match);

    uint32_t getOldestStoredSequence();
    void onMatchRecordAck(const uint8_t* fromMac, const uint8_t* data, size_t dataLen);

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
    void setOpponentIdentity(bool opponentIsHunter, const UUIDBytes& playerUuid,
                             const UUIDBytes& teamUuid);
};




