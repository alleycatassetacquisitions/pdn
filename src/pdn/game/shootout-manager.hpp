#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <vector>
#include "game/player.hpp"
#include "game/chain-manager.hpp"
#include "device/remote-device-coordinator.hpp"
#include "device/wireless-manager.hpp"
#include "utils/simple-timer.hpp"
#include "wireless/resender.hpp"
#include "wireless/wireless-transport.hpp"

class MatchManager;
class WirelessTransport;

// Prefix for shootout-generated match IDs; flags ephemeral (non-persisted) matches.
inline constexpr char kShootoutMatchIdPrefix[] = "SHT-";

// ESP-NOW exposes 20 peer slots per device. The coordinator unicasts the
// bracket and per-match packets to every member, so the bracket can't exceed
// 20 minus the slots held by serial-jack direct peers and the cross-role
// coordinator. RDC does not evict unused peers, so a larger shootout would
// silently fail to register some members.
constexpr uint8_t MAX_SHOOTOUT_MEMBERS = 16;

class ShootoutManager {
public:
    enum class Phase : uint8_t {
        IDLE = 0,
        PROPOSAL = 1,
        BRACKET_REVEAL = 2,
        MATCH_IN_PROGRESS = 3,
        BETWEEN_MATCHES = 4,
        ENDED = 5,
        ABORTED = 6,
    };

    ShootoutManager(Player* player,
                    WirelessManager* wirelessManager,
                    RemoteDeviceCoordinator* rdc,
                    ChainManager* chainManager);
    ~ShootoutManager() = default;

    // Subscribes per-cmd reliable channels on the supplied transport. Must be
    // called once before any send or receive traffic. Tests that don't stand
    // up a transport may leave this unset; sends become no-ops in that case.
    void initialize(WirelessTransport* transport);

    // Optional MatchManager injection. When set, Shootout primes the
    // MatchManager with the duelist pair on each MATCH_START so duel
    // states find a ready match. Tests leave this unset.
    void setMatchManager(MatchManager* matchManager) { matchManager_ = matchManager; }

    bool active() const;
    Phase getPhase() const;

    // Returns the set of MACs that participate in the current loop. Returns
    // empty when not in a loop and no test override is set.
    std::vector<std::array<uint8_t, 6>> getLoopMembers() const;

    // Test-only: override the loop-member set. Pass an empty vector to clear.
    void setLoopMembersForTest(const std::vector<std::array<uint8_t, 6>>& members);

    void startProposal();
    void confirmLocal();
    void sync();
    // CONFIRM payloads carry a fixed-size name field; longer names are
    // truncated, shorter ones null-padded.
    static constexpr size_t kNameLength = ::kNameLength;
    void onConfirmReceived(const uint8_t* fromMac, const char* name = nullptr);
    // Returns the display name for a MAC: the name announced during CONFIRM
    // if known, otherwise the last-two-byte MAC hex suffix.
    std::string getNameForMac(const uint8_t* mac) const;
    size_t getConfirmedCount() const;
    bool hasConfirmed(const uint8_t* mac) const;

    std::array<uint8_t, 6> getCoordinatorMac() const;
    bool isCoordinator() const;
    std::vector<std::array<uint8_t, 6>> getBracket() const;
    bool hasBye() const;

    // Peers with an in-flight send still pending on cmd's reliable channel.
    // Production gates match-start on the bracket count; tests observe the rest.
    size_t pendingAckCount(ShootoutCmd cmd) const;

    // Test seam: cancel a peer's in-flight sends on cmd's channel to simulate it
    // acking. Production acks flow through the channel's deliver path.
    void cancelPendingForTest(ShootoutCmd cmd, const uint8_t* fromMac);

    int getCurrentMatchIndex() const;
    std::pair<std::array<uint8_t,6>, std::array<uint8_t,6>> getCurrentMatchPair() const;

    void onMatchStartReceived(const uint8_t* duelistA, const uint8_t* duelistB,
                              uint8_t matchIndex, uint8_t seqId);
    bool isLocalDuelist() const;
    std::array<uint8_t, 6> getOpponentMac() const;

    void reportLocalWin();
    void onMatchResultReceived(const uint8_t* winner, const uint8_t* loser,
                               uint8_t matchIndex, uint8_t seqId,
                               const uint8_t* fromMac);
    bool isEliminated(const uint8_t* mac) const;

    void onLocalRDCDisconnect(const uint8_t* lostMac);
    void onPeerLostReceived(const uint8_t* lostMac);

    // Fan-in slot for RDC's onDirectPeerChange. Routes disconnect transitions
    // into onLocalRDCDisconnect using the dropped peer's MAC. No-op on connect
    // since Shootout doesn't react to new direct peers mid-tournament.
    void onDirectPeerChange(SerialIdentifier port,
                            std::optional<RemoteDeviceCoordinator::Peer> previous,
                            std::optional<RemoteDeviceCoordinator::Peer> current);
    void onTournamentEndReceived(const uint8_t* winner, uint8_t seqId);
    void onAbortReceived();
    std::array<uint8_t, 6> getTournamentWinner() const;

    // Idempotent on ABORTED/ENDED phases.
    void abortTournament();

    // Reset all tournament state back to IDLE phase so a subsequent loop
    // closure triggers a fresh proposal. Called when the physical ring is
    // broken after TOURNAMENT_END or ABORTED.
    void resetToIdle();

    static constexpr unsigned long kBracketRevealMs = 5000;

private:
    struct NameEntry {
        std::array<uint8_t, 6> mac;
        std::string name;
    };

    // Per-batch buffering for inbound BRACKET_ENTRY. Each batchId reassembles
    // into the local bracket view once every slot has arrived.
    struct PendingBracketBatch {
        // True only while a batch is mid-assembly; cleared when the buffer
        // moves into the bracket, so a retransmitted entry for the finished
        // batchId re-initializes instead of indexing the moved-from buffer
        // (an out-of-bounds write). Also disambiguates idle from batch 0.
        bool active = false;
        uint8_t batchId = 0;
        uint8_t totalSlots = 0;
        std::vector<std::array<uint8_t, 6>> buffer;
        std::vector<bool> receivedSlots;
    };

    // State scoped to a single tournament run. Reset wholesale (tournament_ = {})
    // by resetToIdle() and, minus a few explicit survivors, by the
    // competing-coordinator stand-down. Manager wiring (channels, rdc_, chainManager_,
    // test seams) and the cross-tournament batch-id counter live outside.
    struct TournamentState {
        Phase phase = Phase::IDLE;
        std::vector<std::array<uint8_t, 6>> confirmedSet;
        std::vector<NameEntry> names;
        std::vector<std::array<uint8_t, 6>> bracket;
        // Coord-only working set: starts equal to bracket, replaced by survivors
        // at each round boundary. bracket stays immutable so getBracket() remains
        // stable and non-coord position lookups don't desync.
        std::vector<std::array<uint8_t, 6>> currentRound;
        PendingBracketBatch pendingBracket;
        // Anchored at bracket generation/adoption; see isCoordinator().
        std::array<uint8_t, 6> coordinatorMac{};
        std::array<uint8_t, 6> opponentMac{};
        int currentMatchIndex = -1;
        // currentRound shrinks on round advancement (coordinator only), so cache
        // the duelist pair separately to stay valid on non-coordinators too.
        std::array<uint8_t, 6> currentDuelistA{};
        std::array<uint8_t, 6> currentDuelistB{};
        SimpleTimer bracketRevealTimer;
        std::vector<std::array<uint8_t, 6>> eliminated;
        bool reportedLocalWin = false;
        std::array<uint8_t, 6> tournamentWinner{};
        // Set when Resender abandons BRACKET, MATCH_START, or MATCH_RESULT.
        // Deferred so abortTournament's pending-list mutations don't run inside
        // the Resender::sync() callback chain.
        bool abortPending = false;
    };
    TournamentState tournament_;

    ReliableChannelBase* channelFor(ShootoutCmd cmd) const;

    WirelessTransport* transport_ = nullptr;
    ReliableChannel<ShootoutConfirmPayload, ShootoutCmd>* confirmChannel_ = nullptr;
    ReliableChannel<ShootoutBracketEntryPayload, ShootoutCmd>* bracketEntryChannel_ = nullptr;
    ReliableChannel<ShootoutMatchStartPayload, ShootoutCmd>* matchStartChannel_ = nullptr;
    ReliableChannel<ShootoutMatchResultPayload, ShootoutCmd>* matchResultChannel_ = nullptr;
    ReliableChannel<ShootoutTournamentEndPayload, ShootoutCmd>* tournamentEndChannel_ = nullptr;
    ReliableChannel<ShootoutPeerLostPayload, ShootoutCmd>* peerLostChannel_ = nullptr;
    ReliableChannel<ShootoutAbortPayload, ShootoutCmd>* abortChannel_ = nullptr;

    // Survives tournament resets so a receiver holding a stale batch still
    // sees the next tournament's batch as newer.
    uint8_t nextBracketBatchId_ = 1;
    static bool batchIsNewer(uint8_t a, uint8_t b);

    void onBracketEntryReceived(const uint8_t* fromMac,
                                const ShootoutBracketEntryPayload& p);

    Player* player_;
    WirelessManager* wirelessManager_;
    RemoteDeviceCoordinator* rdc_;
    ChainManager* chainManager_;
    MatchManager* matchManager_ = nullptr;

    void primeMatchManagerForMatch();

    std::vector<std::array<uint8_t, 6>> testLoopMembers_;
    bool testLoopMembersOverride_ = false;

    void recordName(const uint8_t* mac, const char* name);

    std::vector<std::array<uint8_t, 6>> buildLoopMemberSet() const;
    void sendLocalConfirm();
    bool allMembersConfirmed() const;
    void advanceToBracketReveal();
    void generateBracket();

    void sendBracketToPeers();

    void maybeStartNextMatch();
    bool inMaybeStartNextMatch_ = false;
    void sendMatchStartToPeers(int matchIndex);

    // Adopts (a, b, matchIndex) as the current duel and primes MatchManager
    // when self is one of the pair. Returns false when it's already the
    // in-progress match, so a retransmitted MATCH_START can't re-prime.
    bool adoptCurrentMatch(const uint8_t* a, const uint8_t* b, int matchIndex);

    bool isActiveDuelist(const uint8_t* mac) const;
    bool isSameMatch(int matchIndex, const uint8_t* a, const uint8_t* b) const;
    void sendMatchResultToPeers(const uint8_t* winner, const uint8_t* loser,
                              uint8_t matchIndex);
    void applyMatchResult(const uint8_t* winner, const uint8_t* loser);

    void sendTournamentEndToPeers(const uint8_t* winner);
    // Terminal-success transition shared by the coordinator's send path and
    // the follower's TOURNAMENT_END receipt.
    void enterEnded(const uint8_t* winner);
    // Broadcast roster: bracket once assembled, the CONFIRM gather before.
    const std::vector<std::array<uint8_t, 6>>& currentRoster() const {
        return tournament_.bracket.empty() ? tournament_.confirmedSet
                                           : tournament_.bracket;
    }
    std::array<uint8_t, 6> findLastRemaining() const;
};
