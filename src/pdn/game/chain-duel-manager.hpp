#pragma once

#include <array>
#include <atomic>
#include <optional>
#include <vector>
#include <cstdint>
#include <cstring>
#include "game/player.hpp"
#include "device/remote-device-coordinator.hpp"
#include "device/wireless-manager.hpp"
#include "device/drivers/peer-comms-types.hpp"
#include "device/drivers/serial-wrapper.hpp"

enum class ChainGameEventType : uint8_t {
    COUNTDOWN = 0,
    DRAW = 1,
    WIN = 2,
    LOSS = 3,
};

// seqId = 0 is a sentinel meaning "no ACK expected, no retry". Used for
// COUNTDOWN and DRAW which are time-critical and must not arrive late.
// WIN and LOSS use a nonzero seqId and are retransmitted until ACKed or
// the retry budget is exhausted.
//
// championMac names the champion the event came from. Game events go out as one
// broadcast frame, so the sender's MAC says nothing about which chain the event
// belongs to; this field is what each supporter filters on.
struct ChainGameEventPayload {
    uint8_t event_type;
    uint8_t seqId;
    uint8_t championMac[6];
} __attribute__((packed));

class ChainDuelManager {
public:
    ChainDuelManager(Player* player, WirelessManager* wirelessManager, RemoteDeviceCoordinator* rdc);
    virtual ~ChainDuelManager() = default;

    bool isChampion() const;
    bool isSupporter() const;
    virtual bool isLoop() const;
    bool canInitiateMatch() const;
    /// This champion's supporter chain at any depth: the direct supporter-jack
    /// peer, which a cable proves, plus every device that named this one as its
    /// champion in a kChainJoin. Empty inside a ring, which has no champion.
    std::vector<std::array<uint8_t, 6>> getSupporterChainPeers() const;

    void sendGameEventToSupporters(ChainGameEventType eventType);
    void sendConfirm();

    /// Re-sends the confirm this device is standing on to whichever champion it
    /// now holds. No-op until a press has produced one, so a topology event can
    /// never register a supporter who never pressed.
    void resendConfirm();

    /// Supporter-side view of an inbound chain game event. A COUNTDOWN voids the
    /// standing confirm: the champion wipes its roll call at that same moment, so
    /// re-sending the old press would register a supporter for a round it has not
    /// yet answered.
    void onChainGameEventReceived(uint8_t eventType);

    // Supporter-side: ACK a received WIN/LOSS game event back to the
    // champion so it stops retransmitting. Called from GameSession's packet
    // handler when the incoming payload has seqId != 0.
    void sendGameEventAck(const uint8_t* toMac, uint8_t seqId);

    // Champion-side: called when a supporter's ACK arrives. Clears the
    // matching pending game event so retransmission stops.
    void onChainGameEventAckReceived(const uint8_t* fromMac, uint8_t seqId);

    /// True when a broadcast game event names the champion this device follows.
    /// The frame reaches every device in radio range, so this is the only thing
    /// separating our champion's round from a neighbouring chain's.
    bool isEventFromOwnChampion(const uint8_t* eventChampionMac) const;

    /// Champion-side: records `supporterMac` as part of this device's chain, so
    /// a press from a device it shares no cable with can be counted. Ignores a
    /// join naming any champion but this device.
    void onChainJoinReceived(const uint8_t* supporterMac, const uint8_t* joinChampionMac);
    void onConfirmReceived(
        const uint8_t* fromMac,
        const uint8_t* originatorMac,
        uint8_t seqId);
    void onChainStateChanged();

    // Records a peer's role learned from an incoming kRoleAnnounce packet.
    void setPeerRole(SerialIdentifier port, bool isHunter);

    unsigned long getBoostMs() const;
    size_t getConfirmedSupporterCount() const;
    void clearSupporterConfirms();

    const uint8_t* getChampionMac() const;
    void onRoleAnnounceReceived(
        const uint8_t* fromMac,
        uint8_t role,
        const uint8_t* championMac,
        uint8_t seqId);
    void onRoleAnnounceAckReceived(const uint8_t* fromMac, uint8_t seqId);
    void broadcastRoleAndChampion();
    void sendRoleToOpponentJack();
    void sync();

    static constexpr unsigned long BOOST_PER_SUPPORTER_MS = 15;

    // Retry observability for the role-announce channel. Mirrors
    // RemoteDeviceCoordinator::RetryStats semantics.
    struct RetryStats {
        uint32_t sends = 0;
        uint32_t retries = 0;
        uint32_t abandons = 0;
        uint32_t ackLatencyMsSum = 0;
        uint32_t ackCount = 0;
    };
    /// Cumulative retry counters for the role-announce and game-event channels:
    /// ackLatencyMsSum / ackCount is mean RTT, abandons / (sends + retries) is loss.
    RetryStats getRetryStats() const { return retryStats; }

private:
    RetryStats retryStats;
    Player* player;
    WirelessManager* wirelessManager;
    RemoteDeviceCoordinator* rdc;

    SerialIdentifier opponentJack() const;
    SerialIdentifier supporterJack() const;

    // The role/champion cascade. Wrapped by onChainStateChanged so every caller
    // also gets the confirm bookkeeping that has to follow it.
    void applyChainStateChange();

    // Tells the champion this device follows that it exists. The champion cannot
    // discover a supporter it shares no cable with any other way.
    void announceToChampion();

    // Returns the cached role of the direct peer on `port`, or nullopt if no
    // role announcement has been received from the current direct peer.
    std::optional<bool> peerIsHunter(SerialIdentifier port) const;

    // Ceiling shared by the roll call and the supporter roster: the longest
    // supporter chain a duel is built for. Both buffers hold one MAC per
    // supporter, so one number bounds both.
    static constexpr size_t MAX_CHAIN_SUPPORTERS = 18;
    using MacSlots = std::array<std::array<uint8_t, 6>, MAX_CHAIN_SUPPORTERS>;

    // Appends `mac` to a slot buffer, deduped. Full means overwrite oldest, not
    // refuse newest: refusing would let anything on the channel — the packet
    // handler cannot prove a sender is a member — wedge the slots shut and
    // silence every real supporter for the round.
    static void recordMac(MacSlots& slots, std::atomic<size_t>& count,
                          size_t& writeIndex, const uint8_t* mac);
    static bool containsMac(const MacSlots& slots, size_t count, const uint8_t* mac);

    size_t lastSupporterChainCount = 0;

    uint8_t nextConfirmSeqId = 1;  // skip 0 as sentinel

    // Set the moment a press produces a confirm, so the champion-changed and
    // chain-settled triggers know there is something worth re-sending. Atomic:
    // written from the radio task (COUNTDOWN arrival), read from the main loop.
    std::atomic<bool> confirmSent{false};

    // Every press we have heard this round, member or not. Membership is applied
    // when the count is read, not here, because a confirm can beat the kChainJoin
    // that puts its originator in the roster and the sender gets no signal it was
    // dropped. Deciding late means an early confirm starts counting the moment
    // its join lands, and an unplugged supporter stops counting, with nothing to
    // re-offer or evict on a topology event.
    //
    // Fixed slots rather than a vector: packets append to this while a duel is
    // running and the press path scans it to price the shot, so it must not
    // allocate, and the overwrite contract above needs a hard ceiling to be
    // expressible at all.
    MacSlots receivedConfirms{};
    std::atomic<size_t> receivedConfirmCount{0};
    size_t receivedConfirmWrite = 0;

    // Supporters that named this device as their champion. Everything past the
    // direct supporter-jack peer lives here and nowhere else: the role cascade
    // only travels downstream, so a champion has no way to see the far end of
    // its own chain until that end says so.
    MacSlots supporterRoster{};
    std::atomic<size_t> supporterRosterCount{0};
    size_t supporterRosterWrite = 0;

    // Per-port direct peer role; cleared when the direct peer disconnects.
    std::array<std::optional<bool>, 2> peerRoleByPort;

    std::optional<std::array<uint8_t, 6>> championMac;
    std::optional<std::array<uint8_t, 6>> lastAnnouncedSupporterJackMac;
    std::optional<std::array<uint8_t, 6>> lastAnnouncedOpponentJackMac;

    struct PendingRoleAnnounce {
        bool active = false;
        uint8_t seqId = 0;
        uint8_t retries = 0;
        std::array<uint8_t, 6> championMac;
        uint8_t role;
        std::array<uint8_t, 6> targetMac;
        SimpleTimer timer;
    };
    PendingRoleAnnounce pendingRoleAnnounce;
    static constexpr unsigned long kAckTimeoutMs = 100;
    static constexpr uint8_t kMaxRetries = 3;

    uint8_t nextRoleAnnounceSeqId = 1;

    // Champion-side WIN/LOSS in flight. One broadcast frame carries the event to
    // the whole chain, so there is one seqId and one retry schedule; the MACs are
    // only the tally of who still owes an ack. A second event supersedes the
    // first for every supporter at once, so a single slot is the whole state.
    MacSlots pendingEventAcks{};
    size_t pendingEventAckCount = 0;
    uint8_t pendingEventSeqId = 0;
    uint8_t pendingEventType = 0;
    uint8_t pendingEventRetries = 0;
    SimpleTimer pendingEventTimer;

    uint8_t nextGameEventSeqId = 1;
};
