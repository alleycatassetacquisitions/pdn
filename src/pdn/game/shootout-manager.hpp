#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <vector>
#include "game/player.hpp"
#include "device/remote-device-coordinator.hpp"
#include "wireless/resender.hpp"
#include "device/drivers/peer-comms-types.hpp"
#include "device/wireless-manager.hpp"
#include "utils/simple-timer.hpp"

class MatchManager;

// Prefix for shootout-generated match IDs; flags ephemeral (non-persisted) matches.
inline constexpr char kShootoutMatchIdPrefix[] = "SHT-";

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

    /// Subscribes to the coordinator's peer-lost and ring-closed edges when given
    /// one. One callback exists per edge, so at most one ShootoutManager per
    /// coordinator: a second built on the same one takes both slots over, and
    /// whichever is destroyed first empties them for both.
    ShootoutManager(Player* player,
                    WirelessManager* wirelessManager,
                    RemoteDeviceCoordinator* rdc);
    /// Drops the coordinator subscriptions the constructor took, which hold `this`.
    ~ShootoutManager();

    /// Optional MatchManager injection. When set, Shootout primes the
    /// MatchManager with the duelist pair on each MATCH_START so duel
    /// states find a ready match. Tests leave this unset.
    void setMatchManager(MatchManager* manager) { matchManager = manager; }

    bool active() const;
    Phase getPhase() const;

    // Returns the set of MACs that participate in the current loop. Returns
    // empty when not in a loop and no test override is set.
    std::vector<std::array<uint8_t, 6>> getLoopMembers() const;

    // Test-only: override the loop-member set. Pass an empty vector to clear.
    void setLoopMembersForTest(const std::vector<std::array<uint8_t, 6>>& members);

    /// RDC ring-closed observer. The head that detected closure IS the
    /// coordinator — there is no election — so this claims the role, snapshots
    /// the ring roster and announces it to the other members.
    void onRingClosed();
    /// Inbound RING_CLOSED: adopt `fromMac` as coordinator and `members` as the
    /// ring roster. The only proposal trigger a non-coordinator has.
    void onRingClosedReceived(const uint8_t* fromMac,
                              const std::vector<std::array<uint8_t, 6>>& members);
    /// True once a ring closure has been observed and no tournament is running:
    /// the Idle -> ShootoutProposal transition predicate.
    bool shouldEnterProposal() const;

    void startProposal();
    void confirmLocal();
    void sync();
    // Fixed on-wire name length for CONFIRM payloads. Local Player names are
    // null-padded/truncated to this size.
    static constexpr size_t kNameLength = 12;
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

    /// Cumulative retry counters for this manager's command channel. Sends and
    /// retries count frames, abandons count recipients; see Resender::Stats.
    const Resender::Stats& getRetryStats() const { return resender.getStats(); }

    /// Recipients of the fan-out sent under `seqId` that have not yet acked.
    /// Zero once every one of them has answered or been given up on.
    size_t getPendingAckCount(uint8_t seqId) const;
    uint8_t getLastBracketSeqId() const;

    int getCurrentMatchIndex() const;
    /// The two devices of the current match index. Zero MACs only before the
    /// first match; the index is not cleared at a match boundary, so between
    /// matches and after the tournament ends this still names the last pair.
    std::pair<std::array<uint8_t, 6>, std::array<uint8_t, 6>> getCurrentMatchPair() const;

    /// Adopts a bracket announced by the coordinator and acks it. A bracket from
    /// a lower-MAC coordinator also demotes this device.
    void onBracketReceived(const uint8_t* fromMac,
                           const std::vector<std::array<uint8_t, 6>>& offeredBracket,
                           uint8_t seqId);
    void onMatchStartReceived(const uint8_t* duelistA, const uint8_t* duelistB,
                              uint8_t matchIndex, uint8_t seqId);
    bool isLocalDuelist() const;
    std::array<uint8_t, 6> getOpponentMac() const;

    void reportLocalWin();
    /// An ack for one of this manager's fan-outs, named by the seqId it answers.
    void onCommandAckReceived(const uint8_t* fromMac, uint8_t seqId);

    void onMatchResultReceived(const uint8_t* winner, const uint8_t* loser,
                               uint8_t matchIndex, uint8_t seqId,
                               const uint8_t* fromMac);
    /// seqId of the MATCH_RESULT this device most recently sent.
    uint8_t getLastMatchResultSeqId() const { return lastMatchResultSeqId; }
    bool isEliminated(const uint8_t* mac) const;

    void onLocalRDCDisconnect(const uint8_t* lostMac);
    /// Tears down on a peer's PEER_LOST, after checking the named MAC is one of
    /// this device's ring members.
    void onPeerLostReceived(const uint8_t* lostMac);
    uint8_t getLastMatchStartSeqId() const;

    void onTournamentEndReceived(const uint8_t* winner, uint8_t seqId);
    /// seqId of the TOURNAMENT_END this device most recently sent.
    uint8_t getLastTournamentEndSeqId() const { return lastTournamentEndSeqId; }
    /// Tears down on a peer's ABORT. fromMac identifies the sending ring: the
    /// command carries no MACs of its own, and a broadcast reaches every ring
    /// in radio range.
    void onAbortReceived(const uint8_t* fromMac);
    std::array<uint8_t, 6> getTournamentWinner() const;

    // Reset all tournament state back to IDLE phase so a subsequent loop
    // closure triggers a fresh proposal. Called when the physical ring is
    // broken after TOURNAMENT_END or ABORTED.
    void resetToIdle();

    /// Broadcast ABORT to the ring (bracket/confirmedSet), tear down, and land
    /// in Phase::ABORTED. Idempotent: early-returns when already ABORTED.
    void abortTournament();

    static constexpr unsigned long kConfirmRebroadcastMs = 1000;
    static constexpr unsigned long kBracketRevealMs = 5000;
    // Packet-validation clamp on an inbound BRACKET's member count. A ring can
    // hold as many devices as the chain does, so it tracks MAX_CHAIN_MEMBERS;
    // one ESP-NOW v2 frame carries that bracket several times over.
    static constexpr uint8_t MAX_BRACKET_SIZE = MAX_CHAIN_MEMBERS;

private:
    struct NameEntry {
        std::array<uint8_t, 6> mac;
        std::string name;
    };

    Player* player;
    WirelessManager* wirelessManager;
    RemoteDeviceCoordinator* rdc;
    MatchManager* matchManager = nullptr;
    Phase phase = Phase::IDLE;

    void primeMatchManagerForMatch();
    // resetToIdle() clears the ring anchor on top of this; startProposal() does
    // not, because the ring closure that drove the mount established it moments
    // earlier.
    void resetTournamentState();

    uint8_t nextSeqId();
    static bool containsMac(const std::vector<std::array<uint8_t, 6>>& set,
                            const uint8_t* mac);
    // Any of the three, because which set knows the ring depends on the phase:
    // the bracket after reveal, the confirmed set during the proposal, the
    // physical loop before either exists. A follower's bracket is not a subset
    // of its confirmed set, so none of the three subsumes the others.
    bool isRingMember(const uint8_t* mac) const;
    void broadcastCommand(const uint8_t* packet, size_t len);
    void broadcastToRing(const std::vector<std::array<uint8_t, 6>>& peers,
                         const uint8_t* packet, size_t len);
    /// The peers a ring fan-out is addressed to: `peers` without this device.
    std::vector<std::array<uint8_t, 6>> peersExcludingSelf(
        const std::vector<std::array<uint8_t, 6>>& peers) const;
    void sendReliablyToPeers(const std::vector<std::array<uint8_t, 6>>& peers,
                             uint8_t seqId, const uint8_t* packet, size_t len);
    std::vector<std::array<uint8_t, 6>> testLoopMembers;
    bool testLoopMembersOverride = false;
    std::vector<std::array<uint8_t, 6>> confirmedSet;

    // The ring roster as of closure. Read live from the RDC on the coordinator
    // (the only device served a roster) and taken from its RING_CLOSED on every
    // other member. Non-empty is also the "a ring closed" latch.
    std::vector<std::array<uint8_t, 6>> ringMembers;
    SimpleTimer ringClosedRebroadcastTimer;
    void sendRingClosed();

    std::vector<NameEntry> names;
    void recordName(const uint8_t* mac, const char* name);

    std::vector<std::array<uint8_t, 6>> buildLoopMemberSet() const;
    void sendLocalConfirm();
    bool allMembersConfirmed() const;
    void advanceToBracketReveal();
    void generateBracket();

    std::vector<std::array<uint8_t, 6>> bracket;
    // Coord-only working set: starts equal to bracket, replaced by survivors at
    // each round boundary. bracket stays immutable so getBracket() remains
    // stable and non-coord position lookups don't desync.
    std::vector<std::array<uint8_t, 6>> currentRound;

    SimpleTimer confirmRebroadcastTimer;

    uint8_t lastBracketSeqId = 0;
    uint8_t nextShootoutSeqId = 1;

    // Retransmits for every command family this manager sends. Owned here, not
    // shared with the coordinator's: a fan-out armed by this manager must die
    // with it rather than keep broadcasting for a tournament that is over.
    // All four families ride one PktType, so the abandon callback reads which
    // one gave up off the frame's own command byte.
    Resender resender;
    void onCommandAbandoned(uint8_t seqId, const uint8_t* targetMac,
                            const uint8_t* packet, size_t len);

    void sendBracketToPeers();
    // [cmd, seqId, count, count * 6-byte MAC] — the frame BRACKET and
    // RING_CLOSED share.
    std::vector<uint8_t> buildMacListPacket(
        ShootoutCmd cmd, uint8_t seqId,
        const std::vector<std::array<uint8_t, 6>>& macs) const;

    // Anchored at ring closure: self on the head that detected it, the sender on
    // every other member. All-zero means no ring has closed yet.
    std::array<uint8_t, 6> coordinatorMac{};

    std::array<uint8_t, 6> opponentMac{};
    void sendShootoutAck(ShootoutCmd cmd, uint8_t seqId, const uint8_t* toMac);

    int currentMatchIndex = -1;
    // bracket shrinks on round advancement (coordinator only), so cache the
    // duelist pair separately to stay valid on non-coordinators too.
    std::array<uint8_t, 6> currentDuelistA{};
    std::array<uint8_t, 6> currentDuelistB{};
    uint8_t lastMatchStartSeqId = 0;
    SimpleTimer bracketRevealTimer;
    void maybeStartNextMatch();
    bool inMaybeStartNextMatch = false;
    void sendMatchStartToPeers(int matchIndex);
    std::vector<uint8_t> buildMatchStartPacket(int matchIndex) const;

    std::vector<std::array<uint8_t, 6>> eliminated;
    // Ends the tournament for a departed participant. Callers own the question of
    // whether the MAC is one of ours; a locally observed jack loss already is.
    void applyPeerLoss(const uint8_t* lostMac);
    bool isSameMatch(int matchIndex, const uint8_t* a, const uint8_t* b) const;
    bool reportedLocalWin = false;
    // The match whose result this device has already re-sent once, or -1. Keyed
    // on the bout rather than a flag, so it needs no clearing: a later match
    // names a different index and gets its own attempt. A device flag would have
    // to be reset wherever a match turns over, which differs by role.
    int matchResultResentIndex = -1;
    uint8_t lastMatchResultSeqId = 0;
    // Per-command last-observed seqId for ESP-NOW link-layer dedup.
    uint8_t lastObservedBracketSeqId = 0;
    uint8_t lastObservedMatchStartSeqId = 0;
    uint8_t lastObservedTournamentEndSeqId = 0;
    void sendMatchResultToPeers(const uint8_t* winner, const uint8_t* loser,
                                uint8_t matchIndex);
    /// Applies an elimination. `endsCurrentBout` false records it without
    /// leaving MATCH_IN_PROGRESS — a late result for an older bout must not pull
    /// this device out of the one it is fighting now.
    void applyMatchResult(const uint8_t* winner, const uint8_t* loser,
                          bool endsCurrentBout = true);
    std::vector<uint8_t> buildMatchResultPacket(const uint8_t* winner,
                                                const uint8_t* loser,
                                                uint8_t matchIndex) const;

    std::array<uint8_t, 6> tournamentWinner{};
    uint8_t lastTournamentEndSeqId = 0;

    // Snapshot of player->isHunter() at tournament entry: primeMatchManagerForMatch
    // overrides isHunter by MAC ordering, and without restore the override leaks
    // into the post-tournament duel.
    std::optional<bool> originalIsHunter;
    void sendTournamentEndToPeers(const uint8_t* winner);
    std::array<uint8_t, 6> findLastRemaining() const;
};
