/*
    Runs the match handshake and draw for this PDN and keeps qualifying
    standalone matches as 112-byte records, each followed by its 16-byte tag,
    in a 32-slot ring until an FDN collects them.
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
#include "game/record-tag.hpp"
#include "wireless/mac-functions.hpp"
#include "wireless/match-frame-auth.hpp"
#include "wireless/match-packet.hpp"
#include "wireless/record-source.hpp"
#include "wireless/reliable-channel.hpp"
#include "wireless/resender.hpp"
#include "device/drivers/storage-interface.hpp"

class ShootoutManager;

// The match record ring. Matches stored by older firmware live in namespace
// "matches", which this firmware never opens.
inline constexpr const char MATCH_RING_PREFS_NAMESPACE[] = "match_ring";

struct LastMatchDisplay {
    // Effective draw times (raw minus boost); may be negative.
    long myTimeMs = 0;
    long opponentTimeMs = 0;
    unsigned long boostMs = 0;        // boost applied at own button press
    unsigned long penaltyMs = 0;      // early-press penalty applied at own button press
    bool hasData = false;
};

// The opponent's DRAW_RESULT or NEVER_PRESSED as it arrived.
struct OpponentResult {
    int command = 0;
    bool isHunter = false;
    long playerDrawTime = 0;
    unsigned long boostMs = 0;
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
    // Earned at the local button press; taken off the raw time and shown.
    unsigned long boostMs = 0;
    unsigned long penaltyMs = 0;
    // The opponent frame's boost, held to PosseManager::getMaxBoostMs().
    unsigned long opponentBoostMs = 0;
    unsigned long matchLocalStartTime = 0;
    // Set with matchLocalStartTime; a standalone match applies no opponent result before it.
    bool drawWindowIsOpen = false;
    // A standalone opponent's result that came before the draw window; the latest one.
    std::optional<OpponentResult> heldOpponentResult;
    // Signs and checks a standalone match's frames; empty for a shootout match.
    std::optional<MatchFrameAuth> frameAuth;
    unsigned long BUTTON_MASHER_PENALTY_MS = 75;
    int buttonMasherCount = 0;
    std::optional<Match> match;
    std::array<uint8_t, 6> opponentMac = {};

    unsigned long calculateButtonMasherPenalty() {
        return BUTTON_MASHER_PENALTY_MS * buttonMasherCount;
    };
};

class MatchManager : public RecordSource<MatchRecordTraits> {
public:
    /** Claims the kMatchCommand channel on wirelessManager (may be nullptr in tests). */
    explicit MatchManager(WirelessManager* wirelessManager);
    /** Ends the storage session the manager was initialized with. */
    ~MatchManager() override;

    /** Initializes a match and sends SEND_MATCH_ID to opponentMac. */
    void initializeMatch(uint8_t* opponentMac);

    /**
     * Primes a match from a SEND_MATCH_ID that verified under @p frameAuth and
     * replies with MATCH_ID_ACK signed under it.
     */
    void receiveMatch(const char* matchId, const UUIDBytes& opponentPlayerId,
                      const UUIDBytes& opponentTeamId, bool opponentIsHunter, uint8_t* opponentMac,
                      const MatchFrameAuth& frameAuth);

    /**
     * A match secret frame from the coordinator. Only one from the opponent
     * jack, while no match is active, is held for the initiation it names.
     */
    void onMatchSecretFrame(SerialIdentifier jack, const uint8_t* payload, size_t length);

    /**
     * Shootout mode: primes a match without the SEND_MATCH_ID handshake. Both
     * players call this independently on MATCH_START with the same derived
     * match ID and opposite `localIsHunter`, so each writes exactly one of the
     * match's two draw-time slots.
     */
    void initializeShootoutMatch(const char* matchId, uint8_t* opponentMac, bool localIsHunter);

    /** True once the match handshake has completed and countdown may begin. */
    bool isMatchReady() const;

    /**
     * Finalizes the active match and clears active match state. A standalone match
     * is tagged and appended to the ring, unless 32 records already wait there.
     */
    bool finalizeMatch();

    bool setHunterDrawTime(unsigned long hunter_time_ms);
    bool setBountyDrawTime(unsigned long bounty_time_ms);

    /**
     * Records the local timestamp when the match draw window starts, opens the
     * window, and applies an opponent result held until then.
     */
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

    /** Records the ring holds at most; a full ring refuses new matches. */
    static constexpr uint32_t MATCH_RING_CAPACITY = 32;

    /** @return number of the newest stored record (0 when none was ever stored). */
    uint32_t getMatchSequenceNumber();

    /** @return number of the newest record an FDN has collected. */
    uint32_t getCollectedHighWaterMark();

    /**
     * Reads record @p sequenceNumber and its tag into @p record.
     * @return false when that record is not in the ring or its slot cannot be read.
     */
    bool readRecord(uint32_t sequenceNumber, TaggedMatchRecord& record);

    /**
     * Marks every record up to @p sequenceNumber collected, freeing their slots.
     * @return false, changing nothing, when @p sequenceNumber is not a waiting record.
     */
    bool markCollected(uint32_t sequenceNumber);

    /**
     * The oldest waiting record that can be read. An unreadable one is passed
     * over and stays uncollected.
     */
    bool readLowestPending(uint32_t& recordNumber,
                           uint8_t (&recordBytes)[MatchRecordTraits::RECORD_SIZE]) override;

    /** markCollected for a record an FDN acknowledged storing. */
    bool advanceMark(uint32_t recordNumber) override { return markCollected(recordNumber); }

    /** Clears the active match, wipes its secret, and resets all per-match state flags. */
    void clearCurrentMatch();

    /**
     * Clears the active match only when a shootout primed it, leaving a match the
     * cable handshake owns untouched.
     */
    void clearShootoutMatch();

    void setBoostProvider(std::function<unsigned long()> provider);

    /**
     * Borrows the key finalizeMatch tags records with, read when a match is
     * stored; it must outlive the manager. Null or holding no key: the tag is zero.
     */
    void setRecordKey(const RecordKey* recordKey);

    /**
     * Result-screen snapshot that outlives the match, taken whole by finalizeMatch.
     */
    const LastMatchDisplay& getLastMatchDisplay() const { return lastMatchDisplay; }

    // Required for SEND_MATCH_ID to be accepted: the match secret arrives from
    // its opponent jack, and the sender must be the peer there. If unset, every
    // SEND_MATCH_ID is refused and no match can be started.
    void setRemoteDeviceCoordinator(RemoteDeviceCoordinator* rdc);

    // Shootout tournament suppresses the hunter/bounty role-mismatch rejection
    // so same-role matches in the bracket proceed. Injected by GameSession after
    // both managers are built.
    void setShootoutManager(ShootoutManager* shootoutManager);
    ShootoutManager* getShootoutManager() const;

    /**
     * Dispatches an inbound match command after the channel's admit filter:
     * MATCH_ID_ACK, MATCH_ROLE_MISMATCH, DRAW_RESULT or NEVER_PRESSED.
     */
    void listenForMatchEvents(const MatchCommand& command);

    /** Binds the player and storage this manager records matches against. */
    void initialize(Player* player, StorageInterface* storage);

    /** Drives the match channel's retries. */
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

    // The jack the opponent of this device's standing role is cabled to.
    SerialIdentifier opponentJack() const;
    // Queues the secret frame for the opponent jack; false when the coordinator refuses it.
    bool sendMatchSecret(const char* matchId, const MatchSecret& secret);
    // Every inbound frame before duplicate detection: takes SEND_MATCH_ID
    // itself, and admits a standalone match's other frames only once they verify.
    bool admitMatchFrame(const uint8_t* fromMac, const MatchPacket& packet);
    // Holds an initiation from the cabled peer for the secret it needs.
    void onMatchInitiation(const uint8_t* fromMac, const MatchPacket& packet);
    // Primes the match once the held initiation verifies under the held secret.
    void pairInitiationWithSecret();
    void applyOpponentResult(const OpponentResult& result);

    // Bounty: the latest secret off the opponent jack and the match it names.
    struct PendingMatchSecret {
        UUIDBytes matchId;
        MatchFrameAuth frameAuth;
    };
    std::optional<PendingMatchSecret> pendingMatchSecret;
    // Bounty: the latest SEND_MATCH_ID from the cabled peer, until its secret arrives.
    struct PendingInitiation {
        std::array<uint8_t, 6> fromMac;
        MatchPacket packet;
    };
    std::optional<PendingInitiation> pendingInitiation;
    // Set while this manager owns the match secret frame kind on remoteDeviceCoordinator.
    bool matchSecretKindIsRegistered = false;

    Player* player;
    const RecordKey* recordKey = nullptr;

    std::function<unsigned long()> boostProvider;
    RemoteDeviceCoordinator* remoteDeviceCoordinator = nullptr;
    ShootoutManager* shootoutManager = nullptr;

    ActiveMatchState activeMatchState;
    LastMatchDisplay lastMatchDisplay;

    parameterizedCallbackFunction matchButtonPush;
    parameterizedCallbackFunction buttonMasher;

    StorageInterface* storage;
    // Each unreadable record is logged once, not on every FDN beacon.
    uint32_t highestUnreadableReported = 0;
    Resender resender;
    ReliableChannel<MatchPacket> matchChannel;
    /** @return the seqId the frame went out under. */
    uint8_t sendCommand(const uint8_t* mac, int command, const char* matchId, long drawTime,
                        unsigned long boostMs, bool isHunter);
    // The slot's raw draw time minus its player's boost. Needs an active match.
    long getEffectiveDrawTime(bool hunterSlot) const;
    // The recorded winner: a side that never pressed or (for the opponent)
    // never reported loses, else the lower effective time; a tie to the Bounty.
    bool isHunterWinner() const;
    bool appendRecordToStorage(const Match& match);

    // True when the frame names the currently-active match and was sent by
    // that match's opponent. Every post-handshake match command must pass this
    // gate before it changes match state.
    bool isFromActiveMatchOpponent(const uint8_t* fromMac, const char* matchId) const;

    // Emplace match with given id/opponent/draw slot. Common path for both the
    // cable-handshake init and the Shootout MATCH_START init.
    void primeMatch(const char* matchId, const uint8_t* opponentMac, bool localIsHunter);
};
