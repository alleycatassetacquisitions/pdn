#pragma once

#include <array>
#include <functional>
#include <optional>
#include <vector>
#include <cstdint>
#include <cstring>
#include "game/player.hpp"
#include "device/remote-device-coordinator.hpp"
#include "device/wireless-manager.hpp"
#include "device/drivers/peer-comms-types.hpp"
#include "wireless/resender.hpp"
#include "wireless/wireless-transport.hpp"
#include "device/drivers/serial-wrapper.hpp"

enum class ChainGameEventType : uint8_t {
    COUNTDOWN = 0,
    DRAW = 1,
    WIN = 2,
    LOSS = 3,
};

// Game-layer interpretation of the opaque BEACON byte (BeaconRecord::role).
// HAZARD: NONE claims 0, the value every provider-less device (an FDN) floods,
// so 0 must never be read as "bounty" anywhere.
namespace chain_role {

enum class Role : uint8_t { NONE = 0, HUNTER = 1, BOUNTY = 2 };

inline Role fromByte(uint8_t b) {
    switch (b) {
        case static_cast<uint8_t>(Role::HUNTER): return Role::HUNTER;
        case static_cast<uint8_t>(Role::BOUNTY): return Role::BOUNTY;
        default: return Role::NONE;  // unknown values degrade to non-duelist
    }
}

inline uint8_t byteFor(bool isHunter) {
    return static_cast<uint8_t>(isHunter ? Role::HUNTER : Role::BOUNTY);
}

// Head of self's own-role chain: follow opponent-jack mutual edges (OUTPUT
// for a hunter, INPUT for a bounty) of the same role to the chain end. Self
// if self is the head; nullopt inside a closed loop or for a NONE self. A
// NONE node is never traversed and never a candidate; it severs the duel
// chain, so each side of an FDN resolves its own head. Walks the same mutual
// edges as PeerGraph::isInLoop, so role and topology can't disagree.
std::optional<net::Mac> championMac(const PeerGraph& g);

// Hunter/bounty role of the opponent-jack peer; nullopt when the jack is open, the
// link is half-open, the beacon is missing, or either side is NONE (a
// non-duelist neither has nor is a duel opponent). Gates match initiation.
std::optional<bool> mutualOpponentIsHunter(const PeerGraph& g);

}  // namespace chain_role

// All four event types are delivered reliably; SupersedePerTarget keeps that
// safe for the transient ones. Rationale and the send-before-clear ordering
// hazard live at sendGameEventToSupporters in chain-manager.cpp.

class ChainManager {
public:
    ChainManager(Player* player, WirelessManager* wirelessManager, RemoteDeviceCoordinator* rdc);
    virtual ~ChainManager() = default;

    // Champion / supporter / coordinator all derive from one source: the
    // champion resolved by walking the BEACON flood (see
    // chain_role::championMac). No separate role channel, so role and topology
    // can't disagree.
    bool isChampion() const;
    bool isSupporter() const;
    // True on the device that claimed coordinator after observing loop closure.
    virtual bool isCoordinator() const { return isCoordinator_; }
    // True when the local topology is stable AND observes a closed loop. Every
    // ring member sees this independently; Quickdraw uses it to drive each device
    // into ShootoutProposal on loop closure (the spec prompts every participant
    // simultaneously, not just the coordinator).
    virtual bool isInStableLoop() const {
        return rdc_ != nullptr && rdc_->isTopologyStable() && rdc_->isInLoop();
    }
    // Passthrough so state classes can gate on a settled topology without their
    // own rdc reference. Virtual so tests stub it alongside isInStableLoop().
    virtual bool isTopologyStable() const {
        return rdc_ != nullptr && rdc_->isTopologyStable();
    }
    // The topology has SETTLED into a non-loop. The stability term is essential:
    // !isInStableLoop() alone is true mid-churn too, so gating without
    // isTopologyStable() would abort a live tournament on a transient blip.
    bool isRingSettledOpen() const {
        return topologyState() == TopologyState::StableOpen;
    }
    bool canInitiateMatch() const;
    std::vector<std::array<uint8_t, 6>> getSupporterChainPeers() const;

    // Jack on which a same-role opponent would land. OUTPUT_JACK for hunters,
    // INPUT_JACK for bounties. ShootoutManager reads this to find the
    // loop-closing peer regardless of coordinator role.
    SerialIdentifier opponentJack() const;

    const std::vector<std::array<uint8_t, 6>>& getConfirmedSupporters() const {
        return confirmedSupporters_;
    }

    // Idempotent.
    void claimCoordinator();

    // Called by ShootoutManager when a competing BRACKET_ENTRY arrives from a
    // lower-mac peer (tiebreaker for simultaneous ring closure on two cables).
    void demoteCoordinator();

    void sendGameEventToSupporters(ChainGameEventType eventType);
    // True iff a CONFIRM was handed to the reliable channel; false when no
    // champion is resolved (or self is the champion), so callers don't treat
    // an unsendable confirm as confirmed.
    bool sendConfirm();

    // Invoked when a kChainGameEvent arrives from a known supporter-chain sender.
    // The channel auto-acks before this fires; observers don't emit acks.
    using GameEventObserver = std::function<void(uint8_t eventType, const uint8_t* fromMac)>;
    void setGameEventObserver(GameEventObserver cb) { gameEventObserver_ = std::move(cb); }

    bool isKnownGameEventSender(const uint8_t* fromMac) const;
    void onConfirmReceived(
        const uint8_t* fromMac,
        const uint8_t* originatorMac,
        uint8_t seqId);
    void onChainStateChanged();

    unsigned long getBoostMs() const;
    size_t getConfirmedSupporterCount() const;

    // Live supporter-side chain length from topology (see chain-manager.cpp).
    // What the champion's idle screen shows before a duel countdown begins.
    size_t getChainLength() const;

    void clearSupporterConfirms();

    // The champion resolved for this device, or nullopt inside a loop / while
    // unresolved. Pass-through over the RDC façade: one source of truth.
    std::optional<std::array<uint8_t, 6>> getChampionMac() const;

    void sync();

    // Subscribes the per-channel dispatchers on the supplied transport.
    // Must be called once before any receive or send traffic.
    void initialize(WirelessTransport* transport);

    // Fan-in slot for RDC's onDirectPeerChange. Demotes coordinator on
    // disconnect; connect events don't drive coord election (derivation runs
    // continuously from the RDC topology inside sync()).
    void onDirectPeerChange(SerialIdentifier port,
                            std::optional<RemoteDeviceCoordinator::Peer> previous,
                            std::optional<RemoteDeviceCoordinator::Peer> current);

    static constexpr unsigned long BOOST_PER_SUPPORTER_MS = 15;

    using RetryStats = Resender::Stats;
    // Sum across the per-PktType counters this manager owns. The Quickdraw
    // STATS log line reads this for a single rolling view of retry health.
    RetryStats getRetryStats() const {
        if (transport_ == nullptr) return RetryStats{};
        auto b = transport_->getStats(PktType::kChainGameEvent);
        auto c = transport_->getStats(PktType::kChainConfirm);
        RetryStats out{};
        out.sends = b.sends + c.sends;
        out.retries = b.retries + c.retries;
        out.abandons = b.abandons + c.abandons;
        out.ackCount = b.ackCount + c.ackCount;
        out.ackLatencyMsSum = b.ackLatencyMsSum + c.ackLatencyMsSum;
        return out;
    }

private:
    Player* player_;
    WirelessManager* wirelessManager_;
    RemoteDeviceCoordinator* rdc_;

    SerialIdentifier supporterJack() const;

    bool championIsSelf(const std::array<uint8_t, 6>& champ) const;

    // Single home for "what is this device in the chain?". Coordinator is
    // exclusive (a ring member that claimed coordinator holds no
    // champion-relative role), expressed as a value rather than a per-predicate
    // guard. isChampion()/isSupporter() read over this; isCoordinator() stays a
    // separate virtual flag reader since tests stub it.
    enum class ChainRole { None, Coordinator, Champion, Supporter };
    ChainRole chainRole() const;

    // Single home for "what is this device's loop/ring topology?". The three
    // public predicates are slices of this. Composed from the two virtual readers
    // (not rdc_ directly) so a test stubbing isInStableLoop()/isTopologyStable()
    // drives every slice consistently.
    enum class TopologyState { Unstable, StableLoop, StableOpen };
    TopologyState topologyState() const {
        if (!isTopologyStable()) return TopologyState::Unstable;
        return isInStableLoop() ? TopologyState::StableLoop
                                : TopologyState::StableOpen;
    }

    std::vector<std::array<uint8_t, 6>> confirmedSupporters_;

    bool isCoordinator_ = false;

    WirelessTransport* transport_ = nullptr;
    ReliableChannel<ChainGameEventPayload>* gameEventChannel_ = nullptr;
    ReliableChannel<ChainConfirmPayload>* confirmChannel_ = nullptr;
    GameEventObserver gameEventObserver_;
    void onGameEventReceived(const uint8_t* fromMac, const ChainGameEventPayload& p);

    // Coord-eligibility derivation state, updated once per ~1Hz sync() cycle.
    // lastStableMin_ is the prior lowest MAC in getChainMembers();
    // stableMinCycles_ counts consecutive cycles it matched. Claim is gated on
    // stableMinCycles_ >= 1, a 1-cycle stability window.
    std::optional<std::array<uint8_t, 6>> lastStableMin_;
    int stableMinCycles_ = 0;
    unsigned long nextMinStabilityCheckMs_ = 0;
    static constexpr unsigned long kCoordStabilityCycleMs = 1000;

    // Self-healing confirm backstop. The immediate confirm a supporter fires on
    // a topology change can be dropped by the champion's stability gate before
    // the graph settles; without a periodic re-send the supporter silently
    // contributes zero boost. The champion dedups by originator MAC, so a steady
    // 1Hz repeat carries no storm.
    unsigned long nextConfirmBackstopMs_ = 0;
    static constexpr unsigned long kConfirmBackstopMs = 1000;

    void deriveCoordinator();
    unsigned long nowMs() const;
};
