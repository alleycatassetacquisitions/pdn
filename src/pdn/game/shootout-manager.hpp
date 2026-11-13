#pragma once

#include <array>
#include <cstdint>
#include <vector>
#include "game/player.hpp"
#include "device/remote-device-coordinator.hpp"
#include "device/drivers/peer-comms-types.hpp"
#include "device/wireless-manager.hpp"
#include "utils/debounced-condition.hpp"
#include "utils/simple-timer.hpp"
#include "wireless/reliable-channel.hpp"
#include "wireless/shootout-packets.hpp"

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

    /**
     * Claims the seven shootout PktTypes on wirelessManager's transport (so one
     * manager per device) and subscribes to the coordinator's ring-closed edge
     * when given one. A non-null matchManager is told this manager exists, so
     * it can tell a tournament is live, and is primed with the player pair on
     * each MATCH_START.
     */
    ShootoutManager(Player* player, WirelessManager* wirelessManager, RemoteDeviceCoordinator* rdc,
                    MatchManager* matchManager);
    /** Drops the registrations the constructor took, which hold `this`. */
    ~ShootoutManager();

    bool active() const;
    Phase getPhase() const;

    /**
     * Participants in the current ring: live RDC roster + self on the ring head,
     * or the latched RING_CLOSED roster.
     */
    std::vector<std::array<uint8_t, 6>> getRingMembers() const;

    void onRingClosed();
    void onRingClosedReceived(const uint8_t* fromMac,
                              const std::vector<std::array<uint8_t, 6>>& members);
    /**
     * True while this device sits on a closed ring and no tournament is running:
     * the Idle -> ShootoutProposal transition predicate.
     */
    bool shouldEnterProposal() const;

    void startProposal();
    void confirmLocal();
    void sync();
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

    int getCurrentMatchIndex() const;
    /**
     * The two devices of the current match index. Zero MACs only before the
     * first match; the index is not cleared at a match boundary, so between
     * matches and after the tournament ends this still names the last pair.
     */
    std::pair<std::array<uint8_t, 6>, std::array<uint8_t, 6>> getCurrentMatchPair() const;

    void onBracketReceived(const uint8_t* fromMac,
                           const std::vector<std::array<uint8_t, 6>>& offeredBracket,
                           uint8_t seqId);
    /**
     * Inbound MATCH_START. Admitted on `fromMac` being our coordinator, which
     * is the authority BRACKET propagated; the player pair is game content and
     * cannot answer whether a broadcast frame is ours.
     */
    void onMatchStartReceived(const uint8_t* fromMac, const uint8_t* playerA,
                              const uint8_t* playerB, uint8_t matchIndex, uint8_t seqId);
    /** True when this device is one of the two players in the current match. */
    bool isInCurrentMatch() const;
    std::array<uint8_t, 6> getOpponentMac() const;

    void reportLocalWin();

    /** Inbound MATCH_RESULT, admitted on `fromMac` being in this device's bracket. */
    void onMatchResultReceived(const uint8_t* winner, const uint8_t* loser, uint8_t matchIndex,
                               uint8_t seqId, const uint8_t* fromMac);
    /** True once `mac` has lost a match in this tournament. */
    bool isEliminated(const uint8_t* mac) const;

    /**
     * Inbound TOURNAMENT_END, admitted on `fromMac` for the same reason as
     * onMatchStartReceived.
     */
    void onTournamentEndReceived(const uint8_t* fromMac, const uint8_t* winner, uint8_t seqId);
    /**
     * Tears down on a peer's ABORT. fromMac identifies the sending ring: the
     * command carries no MACs of its own, and a broadcast reaches every ring
     * in radio range.
     */
    void onAbortReceived(const uint8_t* fromMac, uint8_t seqId);
    std::array<uint8_t, 6> getTournamentWinner() const;

    // Reset all tournament state back to IDLE phase so a subsequent ring
    // closure triggers a fresh proposal. Called when the physical ring is
    // broken after TOURNAMENT_END or ABORTED.
    void resetToIdle();

    /**
     * Fans ABORT out to the ring (bracket, or confirmedSet before a bracket
     * exists), then tears down into Phase::ABORTED. Idempotent, and refuses
     * ENDED as well as ABORTED.
     */
    void abortTournament();

    /**
     * Ring-break debounce. A cable nudge flickers the ring on real hardware; act
     * only once the break has settled. This covers the flicker, not a recovery:
     * healing a nudge costs a link re-handshake, the latching device's own MAC
     * walking the ring to re-latch, then the flag walking back, all of which
     * outlast it. Calibrated against how long a nudge flickers, so it is not the
     * RDC's propagation window even though both are 500 — widening this one is a
     * rig question and does not imply widening that one.
     */
    static constexpr unsigned long RING_BREAK_DEBOUNCE_MS = 500;
    /**
     * Confirmed members needed to draw a bracket. Two is the structural floor —
     * a match needs two players — so a two-device ring is exactly at it and runs.
     */
    static constexpr size_t MIN_PARTICIPANTS = 2;
    /**
     * How long a roster stays below MIN_PARTICIPANTS before the proposal is
     * given up on. Wall-clock, and generous: what fills the roster is members
     * announcing themselves to the head, which nothing here drives or can
     * predict. Raise it if a venue's larger rings are seen to fill slower.
     */
    static constexpr unsigned long SHORT_ROSTER_TIMEOUT_MS = 3000;
    static constexpr unsigned long kConfirmRebroadcastMs = 1000;
    static constexpr unsigned long kBracketRevealMs = 5000;

    // Keeps the reveal window the binding wait between the two gates in
    // maybeStartNextMatch. Invert it and the ack gate becomes the only thing
    // holding the start, with no timeout of its own to resolve it.
    static_assert(kBracketRevealMs > Resender::RETRANSMIT_SPAN_MS,
                  "the bracket-ack gate would hold the tournament with no timeout");

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
    void resetTournamentState();

    static bool containsMac(const std::vector<std::array<uint8_t, 6>>& set,
                            const uint8_t* mac);
    /** True when `mac` is the coordinator this device is following. */
    bool isFromCoordinator(const uint8_t* mac) const;
    bool isTerminalPhase() const {
        return phase == Phase::ENDED || phase == Phase::ABORTED;
    }
    // Any of the three, because which set knows the ring depends on the phase:
    // the bracket after reveal, the confirmed set during the proposal, the
    // physical ring before either exists. A follower's bracket is not a subset
    // of its confirmed set, so none of the three subsumes the others.
    bool isRingMember(const uint8_t* mac) const;
    /** The peers a ring fan-out is addressed to: `peers` without this device. */
    std::vector<std::array<uint8_t, 6>>
    peersExcludingSelf(const std::vector<std::array<uint8_t, 6>>& peers) const;
    std::vector<std::array<uint8_t, 6>> confirmedSet;

    // The ring roster as of closure. Read live from the RDC on the head that
    // latched it, and taken from that head's RING_CLOSED on every other member.
    // Non-empty is also the "a ring closed" latch, and nothing retires it while
    // IDLE, so shouldEnterProposal pairs it with a liveness check.
    std::vector<std::array<uint8_t, 6>> ringMembers;
    DebouncedCondition ringBreakDebounce;
    DebouncedCondition shortRosterDebounce;
    SimpleTimer ringClosedRebroadcastTimer;
    void sendRingClosed();

    std::vector<NameEntry> names;
    void recordName(const uint8_t* mac, const char* name);

    bool headsRing() const;
    const uint8_t* ringHead() const;
    void sendLocalConfirm();
    bool allMembersConfirmed() const;
    /**
     * Overload for a roster the caller already has, so a tick that asks several
     * questions of it rebuilds it once.
     */
    bool allMembersConfirmed(const std::vector<std::array<uint8_t, 6>>& members) const;
    void advanceToBracketReveal();
    void generateBracket();

    std::vector<std::array<uint8_t, 6>> bracket;
    // Coord-only working set: starts equal to bracket, replaced by survivors at
    // each round boundary. bracket stays immutable so getBracket() remains
    // stable and non-coord position lookups don't desync.
    std::vector<std::array<uint8_t, 6>> currentRound;

    SimpleTimer confirmRebroadcastTimer;

    uint8_t lastBracketSeqId = 0;

    // One FAN_OUT channel per command, each frame one broadcast. RING_CLOSED
    // and CONFIRM go unsequenced and unacked; the other five are acked per
    // recipient.
    ReliableChannel<ShootoutRosterPayload> ringClosedChannel;
    ReliableChannel<ShootoutConfirmPayload> confirmChannel;
    ReliableChannel<ShootoutRosterPayload> bracketChannel;
    ReliableChannel<ShootoutMatchStartPayload> matchStartChannel;
    ReliableChannel<ShootoutMatchResultPayload> matchResultChannel;
    ReliableChannel<ShootoutTournamentEndPayload> tournamentEndChannel;
    ReliableChannel<ShootoutAbortPayload> abortChannel;
    // The RDC's 64-member roster plus self can reach 65, one past
    // ShootoutRosterPayload::macs; the last entry is dropped rather than overflowing.
    static ShootoutRosterPayload buildRoster(const std::vector<std::array<uint8_t, 6>>& macs);
    static std::vector<std::array<uint8_t, 6>> unpackRoster(const ShootoutRosterPayload& roster);
    /** Broadcasts an unsequenced frame only when `peers` names someone besides this device. */
    template <class P>
    void broadcastToRing(ReliableChannel<P>& channel,
                         const std::vector<std::array<uint8_t, 6>>& peers, P& payload);

    void sendBracketToPeers();

    // Author of the current bracket; all-zero while no bracket is held.
    std::array<uint8_t, 6> coordinatorMac{};

    std::array<uint8_t, 6> opponentMac{};

    // Never repeats within a tournament, so a stale frame for an earlier match
    // stays recognisable. roundPairIndex is the pair's slot in currentRound.
    int currentMatchIndex = -1;
    int roundPairIndex = -1;
    // currentRound shrinks on round advancement (coordinator only), so cache the
    // player pair separately to stay valid on non-coordinators too.
    std::array<uint8_t, 6> currentPlayerA{};
    std::array<uint8_t, 6> currentPlayerB{};
    SimpleTimer bracketRevealTimer;
    /**
     * Advances the bracket. Called only from sync(), on the main loop — a second
     * caller on the packet path would need its own guard against double-advance.
     */
    void maybeStartNextMatch();
    void sendMatchStartToPeers();

    std::vector<std::array<uint8_t, 6>> eliminated;
    bool reportedLocalWin = false;
    // The match whose result this device has already re-sent once, or -1. Keyed
    // on the match rather than a flag, so only a tournament reset clears it: a
    // later match names a different index and gets its own attempt. A device flag
    // would have to be reset wherever a match turns over, which differs by role.
    int matchResultResentIndex = -1;
    void sendMatchResultToPeers(const uint8_t* winner, const uint8_t* loser,
                                uint8_t matchIndex);
    /**
     * Applies an elimination. `endsCurrentMatch` false records it without
     * leaving MATCH_IN_PROGRESS — a late result for an older match must not pull
     * this device out of the one it is fighting now.
     */
    void applyMatchResult(const uint8_t* winner, const uint8_t* loser,
                          bool endsCurrentMatch = true);

    std::array<uint8_t, 6> tournamentWinner{};
    void sendTournamentEndToPeers(const uint8_t* winner);
};
