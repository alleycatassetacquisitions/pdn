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
#include <cstdint>
#include <cstring>
#include <functional>
#include <optional>
#include <string>
#include "device/drivers/button.hpp"
#include "device/remote-device-coordinator.hpp"
#include "game/match.hpp"
#include "game/player.hpp"
#include "id-generator.hpp"
#include "wireless/reliable-channel.hpp"
#include "device/drivers/storage-interface.hpp"

class ShootoutManager;
class WirelessTransport;

// Wire format transmitted over ESP-NOW for every quickdraw command.
// seqId is stamped by the reliable channel on send for ack matching;
// 0 means "fire-and-forget, no ack expected".
struct QuickdrawPacket {
    char matchId[37];  // IdGenerator::UUID_BUFFER_SIZE
    char playerId[5];  // 4 chars + null terminator
    bool isHunter;
    long playerDrawTime;
    int  command;
    uint8_t seqId;
} __attribute__((packed));

enum QDCommand {
    SEND_MATCH_ID = 0,
    MATCH_ID_ACK = 1,
    MATCH_ROLE_MISMATCH = 2,
    DRAW_RESULT = 3,
    NEVER_PRESSED = 4,
};

// Builds a wire packet. seqId is normally stamped by the reliable channel on
// send; the parameter exists for tests that inject received packets directly.
inline QuickdrawPacket makeQuickdrawPacket(int command, const char* matchId,
                                           const char* playerId,
                                           long playerDrawTime, bool isHunter,
                                           uint8_t seqId = 0) {
    QuickdrawPacket p{};
    p.command = command;
    p.playerDrawTime = playerDrawTime;
    p.isHunter = isHunter;
    p.seqId = seqId;
    strncpy(p.matchId, matchId, sizeof(p.matchId) - 1);
    strncpy(p.playerId, playerId, sizeof(p.playerId) - 1);
    return p;
}

// Preferences namespace and keys

struct LastMatchDisplay {
    unsigned long myTimeMs = 0;       // boosted draw time, as used for winner calc
    unsigned long opponentTimeMs = 0;
    unsigned long boostMs = 0;        // boost applied at own button press
    bool hasData = false;
};

struct ActiveDuelState {
    bool matchIsReady = false;
    // Each side's duel outcome, resolved once (first writer wins). Absent means
    // not yet known; pressed=false means that side timed out and its draw time
    // is the pity time. "Pressed" and "timed out" are the same field, so the
    // contradiction (pressed AND timed-out) cannot be represented.
    struct SideResult { bool pressed; };
    std::optional<SideResult> myResult;
    std::optional<SideResult> theirResult;
    unsigned long duelLocalStartTime = 0;
    unsigned long BUTTON_MASHER_PENALTY_MS = 75;
    int buttonMasherCount = 0;
    std::optional<Match> match;
    std::array<uint8_t, 6> opponentMac = {};
    // This device's draw-slot for THIS match (which of hunter/bounty_draw_time it
    // writes), captured at match init. Normal duel: equals the player's global
    // role. Same-role shootout duel: MAC-ordered so two hunters still slot
    // into distinct draw times. Kept off Player::isHunter() so the shootout never
    // mutates the global role.
    bool localIsHunter = false;
    // Shootout matches are venue-local: no persistence, no upload, no career
    // stats, no chain boost. Set only by initializeShootoutMatch, never
    // derived from wire data, so a peer can't mark a normal duel ephemeral.
    bool isShootout = false;

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

    // Shootout mode: prime a match without the SEND_MATCH_ID handshake.
    // Both duelists call this independently on MATCH_START with the same
    // derived match ID. localIsHunter is the MAC-ordered per-match draw-slot
    // (both sides compute the same ordering), so same-role ring devices still
    // form a valid hunter-vs-bounty pairing without touching the global role.
    void initializeShootoutMatch(const char* matchId, uint8_t* opponentMac,
                                 bool localIsHunter);

    // This device's draw-slot for the active match (see ActiveDuelState).
    // Falls back to the global hunter/bounty role when no match is active.
    bool localIsHunterForMatch() const {
        return activeDuelState.match.has_value() ? activeDuelState.localIsHunter
                                                 : player->isHunter();
    }

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

    // Shootout tournament suppresses the hunter/bounty role-mismatch rejection
    // so same-role duels in the bracket proceed. Injected by Quickdraw after
    // both managers are built.
    void setShootoutManager(ShootoutManager* shootoutManager);
    ShootoutManager* getShootoutManager() const;

    void listenForMatchEvents(const uint8_t* fromMac, const QuickdrawPacket& packet);

    // Claims the (kQuickdrawCommand, 0) reliable channel on the transport and
    // installs its receive/abandon handlers. A null transport (tests, devices
    // without a radio) leaves the channel unset; sends become no-ops.
    void initialize(Player* player, StorageInterface* storage, WirelessTransport* transport);

    parameterizedCallbackFunction getDuelButtonPush();

    parameterizedCallbackFunction getButtonMasher();

    void sendNeverPressed(unsigned long pityTime);

    void voidCurrentMatch();
    bool isVoided() const {
        return activeDuelState.match.has_value() && activeDuelState.match->isVoided();
    }

    // Shootout matches are venue-local: they never persist or upload, so they
    // must not touch lifetime career stats either. Gates the same way
    // finalizeMatch() gates persistence, keeping the two consistent.
    bool currentMatchIsShootout() const;

    // A reliable send is abandoned only after exhausting its retry budget.
    // No-ops unless `target` is the current opponent, so a stale abandon for a
    // prior peer can't void a match primed against a new one.
    void onReliableSendAbandoned(const uint8_t* target);

    // Public because the button-push callback (getDuelButtonPush) is a free
    // function with no access to the private duel state; every other duel-state
    // transition is internal.
    void setReceivedButtonPush();


private:

    Player* player;

    std::function<unsigned long()> boostProvider_;
    RemoteDeviceCoordinator* rdc_ = nullptr;
    ShootoutManager* shootoutManager_ = nullptr;

    ActiveDuelState activeDuelState;
    LastMatchDisplay lastMatchDisplay_;

    parameterizedCallbackFunction duelButtonPush;
    parameterizedCallbackFunction buttonMasher;

    StorageInterface* storage;
    ReliableChannel<QuickdrawPacket>* channel_ = nullptr;

    // Reliable unicast on the quickdraw channel; the channel stamps a fresh
    // seqId into the packet and the transport's resender retries until acked
    // or the retry budget routes to onReliableSendAbandoned.
    void sendReliable(const uint8_t* macAddress, const QuickdrawPacket& packet);
    // Packet stamped with this device's own player ID, the common case for
    // every outbound command.
    QuickdrawPacket makeOwnPacket(int command, const char* matchId,
                                  long playerDrawTime, bool isHunter) const {
        return makeQuickdrawPacket(command, matchId, player->getUserID().c_str(),
                                   playerDrawTime, isHunter);
    }
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
    bool isFromActiveMatchOpponent(const uint8_t* fromMac,
                                   const QuickdrawPacket& packet) const;

    void sendMatchAck();
    void sendMatchId();
    void sendMatchRoleMismatch(const uint8_t* fromMac, const QuickdrawPacket& incoming);

    // Emplace match with given id/opponent. Common path for both the normal
    // duel init (the wireless SEND_MATCH_ID handshake, triggered by cable
    // connect) and the Shootout MATCH_START init.
    void primeMatch(const char* matchId, const uint8_t* opponentMac, bool localIsHunter);
};




