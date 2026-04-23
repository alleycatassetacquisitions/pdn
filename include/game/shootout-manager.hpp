#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <vector>
#include "game/player.hpp"
#include "game/chain-duel-manager.hpp"
#include "device/remote-device-coordinator.hpp"
#include "device/wireless-manager.hpp"
#include "utils/simple-timer.hpp"

class MatchManager;

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
                    ChainDuelManager* cdm);
    ~ShootoutManager() = default;

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

    void onBracketAckReceived(const uint8_t* fromMac, uint8_t seqId);
    uint8_t getLastBracketSeqId() const;
    size_t getBracketPendingAckCount() const;

    int getCurrentMatchIndex() const;
    std::pair<std::array<uint8_t,6>, std::array<uint8_t,6>> getCurrentMatchPair() const;
    void onMatchStartAckReceived(const uint8_t* fromMac, uint8_t seqId);

    void onBracketReceived(const std::vector<std::array<uint8_t, 6>>& bracket, uint8_t seqId);
    void onMatchStartReceived(const uint8_t* duelistA, const uint8_t* duelistB,
                              uint8_t matchIndex, uint8_t seqId);
    bool isLocalDuelist() const;
    std::array<uint8_t, 6> getOpponentMac() const;

    void reportLocalWin();
    void onMatchResultReceived(const uint8_t* winner, const uint8_t* loser,
                               uint8_t matchIndex, uint8_t seqId,
                               const uint8_t* fromMac);
    void onMatchResultAckReceived(const uint8_t* fromMac, uint8_t seqId);
    size_t getMatchResultPendingAckCount() const;
    uint8_t getLastMatchResultSeqId() const { return lastMatchResultSeqId_; }
    bool isEliminated(const uint8_t* mac) const;

    void onLocalRDCDisconnect(const uint8_t* lostMac);
    void onPeerLostReceived(const uint8_t* lostMac);
    bool isForfeited(const uint8_t* mac) const;
    uint8_t getLastMatchStartSeqId() const;

    void onTournamentEndReceived(const uint8_t* winner, uint8_t seqId);
    void onTournamentEndAckReceived(const uint8_t* fromMac, uint8_t seqId);
    size_t getTournamentEndPendingAckCount() const;
    uint8_t getLastTournamentEndSeqId() const { return lastTournamentEndSeqId_; }
    void onAbortReceived();
    std::array<uint8_t, 6> getTournamentWinner() const;

    // Reset all tournament state back to IDLE phase so a subsequent loop
    // closure triggers a fresh proposal. Called when the physical ring is
    // broken after TOURNAMENT_END or ABORTED.
    void resetToIdle();

    static constexpr unsigned long kConfirmRebroadcastMs = 1000;
    static constexpr unsigned long kBracketRevealMs = 5000;
    static constexpr unsigned long kMatchWatchdogMs = 10000;
    // Absolute upper bound on bracket size. RDC peer-table capacity and
    // kMaxChainPeersPerPort=18 cap real hardware tournaments well below this;
    // value is a packet-validation clamp to reject malformed BRACKET packets.
    static constexpr uint8_t kMaxBracketSize = 32;

private:
    struct BracketPending {
        std::array<uint8_t, 6> peer;
        uint8_t retries = 0;
        SimpleTimer timer;
    };

    struct NameEntry {
        std::array<uint8_t, 6> mac;
        std::string name;
    };

    Player* player_;
    WirelessManager* wirelessManager_;
    RemoteDeviceCoordinator* rdc_;
    ChainDuelManager* cdm_;
    MatchManager* matchManager_ = nullptr;
    Phase phase_ = Phase::IDLE;

    void primeMatchManagerForMatch();

    uint8_t nextSeqId();
    void sendToPeers(const std::vector<std::array<uint8_t, 6>>& peers,
                     const uint8_t* packet, size_t len);
    void sendReliablyToPeers(std::vector<BracketPending>& pending,
                             const std::vector<std::array<uint8_t, 6>>& peers,
                             const uint8_t* packet, size_t len);
    static void eraseFromPending(std::vector<BracketPending>& pending,
                                 const uint8_t* fromMac);

    std::vector<std::array<uint8_t, 6>> testLoopMembers_;
    bool testLoopMembersOverride_ = false;
    std::vector<std::array<uint8_t, 6>> confirmedSet_;

    std::vector<NameEntry> names_;
    void recordName(const uint8_t* mac, const char* name);

    std::vector<std::array<uint8_t, 6>> buildLoopMemberSet() const;
    void sendLocalConfirm();
    bool allMembersConfirmed() const;
    void advanceToBracketReveal();
    void generateBracket();

    std::vector<std::array<uint8_t, 6>> bracket_;
    // Coord-only working set: starts equal to bracket_, replaced by survivors at
    // each round boundary. bracket_ stays immutable so getBracket() remains
    // stable and non-coord position lookups don't desync.
    std::vector<std::array<uint8_t, 6>> currentRound_;

    SimpleTimer confirmRebroadcastTimer_;

    std::vector<BracketPending> bracketPendingAcks_;
    uint8_t lastBracketSeqId_ = 0;
    uint8_t nextShootoutSeqId_ = 1;
    static constexpr uint8_t kMaxShootoutAckRetries = 3;

    void sendBracketToPeers();
    // Returns true iff the retry budget is exhausted for this peer and the
    // caller should abort the tournament after exiting its iteration.
    bool retryBracketForPeer(BracketPending& p);
    void abortTournament();
    std::vector<uint8_t> buildBracketPacket() const;
    static std::array<uint8_t, 6> lowestMacIn(
        const std::vector<std::array<uint8_t, 6>>& set);

    std::array<uint8_t, 6> opponentMac_{};
    void sendShootoutAck(ShootoutCmd cmd, uint8_t seqId, const uint8_t* toMac);

    int currentMatchIndex_ = -1;
    // bracket_ shrinks on round advancement (coordinator only), so cache the
    // duelist pair separately to stay valid on non-coordinators too.
    std::array<uint8_t, 6> currentDuelistA_{};
    std::array<uint8_t, 6> currentDuelistB_{};
    uint8_t lastMatchStartSeqId_ = 0;
    SimpleTimer bracketRevealTimer_;
    std::vector<BracketPending> matchStartPendingAcks_;
    void maybeStartNextMatch();
    bool inMaybeStartNextMatch_ = false;
    void sendMatchStartToPeers(int matchIndex);
    std::vector<uint8_t> buildMatchStartPacket(int matchIndex) const;

    std::vector<std::array<uint8_t, 6>> eliminated_;
    std::vector<std::array<uint8_t, 6>> forfeited_;
    bool isActiveDuelist(const uint8_t* mac) const;
    bool isEliminatedOrForfeited(const uint8_t* mac) const;
    uint8_t lastMatchResultSeqId_ = 0;
    // Per-command last-observed seqId for ESP-NOW link-layer dedup.
    uint8_t lastObservedBracketSeqId_ = 0;
    uint8_t lastObservedMatchStartSeqId_ = 0;
    uint8_t lastObservedTournamentEndSeqId_ = 0;
    SimpleTimer matchStartWatchdog_;
    void sendMatchResultToPeers(const uint8_t* winner, const uint8_t* loser,
                              uint8_t matchIndex);
    std::vector<BracketPending> matchResultPendingAcks_;
    // Cached so sync() can rebuild the packet for retry. Senders aren't
    // always the coordinator, so a per-sender cache is required.
    struct LastMatchResult {
        std::array<uint8_t, 6> winner{};
        std::array<uint8_t, 6> loser{};
        uint8_t matchIndex = 0;
    };
    LastMatchResult lastMatchResult_;
    void applyMatchResult(const uint8_t* winner, const uint8_t* loser);
    std::vector<uint8_t> buildMatchResultPacket(const uint8_t* winner,
                                                const uint8_t* loser,
                                                uint8_t matchIndex) const;

    static unsigned long ackTimeoutForRetry(uint8_t retries);

    std::array<uint8_t, 6> tournamentWinner_{};
    uint8_t lastTournamentEndSeqId_ = 0;

    // Snapshot of player_->isHunter() at tournament entry: primeMatchManagerForMatch
    // overrides isHunter by MAC ordering, and without restore the override leaks
    // into the post-tournament duel.
    std::optional<bool> originalIsHunter_;
    void sendTournamentEndToPeers(const uint8_t* winner);
    std::vector<BracketPending> tournamentEndPendingAcks_;
    std::array<uint8_t, 6> findLastRemaining() const;
};
