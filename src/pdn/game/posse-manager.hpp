#pragma once

#include <array>
#include <optional>
#include <vector>
#include <cstdint>
#include <cstring>
#include "game/player.hpp"
#include "device/remote-device-coordinator.hpp"
#include "device/wireless-manager.hpp"
#include "device/drivers/peer-comms-types.hpp"
#include "device/drivers/serial-wrapper.hpp"
#include "wireless/reliable-channel.hpp"

enum class ChainGameEventType : uint8_t {
    COUNTDOWN = 0,
    DRAW = 1,
    WIN = 2,
    LOSS = 3,
};

// seqId = 0 is a sentinel meaning "no ACK expected, no retry". Used for
// COUNTDOWN and DRAW which are time-critical and must not arrive late.
// WIN and LOSS use a nonzero seqId and are retransmitted until ACKed or
// the frame's span runs out.
//
// challengerMac names the challenger the event came from. Game events go out as one
// broadcast frame, so every chain in radio range hears it; a supporter takes a
// frame only when this field and the frame's source MAC both name the
// challenger it follows.
struct ChainGameEventPayload {
    uint8_t event_type;
    uint8_t seqId;
    uint8_t challengerMac[6];
} __attribute__((packed));

class PosseManager {
public:
    /** A chain game event from the challenger this device follows, after the posse has applied it.
     */
    using GameEventListener = std::function<void(uint8_t eventType)>;

    /** Requires a live rdc and owns its single-subscriber topology/context callback slots. */
    PosseManager(Player* player, WirelessManager* wirelessManager, RemoteDeviceCoordinator* rdc);
    /** Drops the coordinator subscriptions the constructor took, which hold `this`. */
    virtual ~PosseManager();

    /** True when this device is the Challenger at the head of its Posse chain. */
    bool isChallenger() const;
    bool isSupporter() const;
    bool canInitiateMatch() const;
    /**
     * The jack this device's opponent cables to: output for a standing hunter,
     * input for a standing bounty. The other one carries its supporters.
     */
    SerialIdentifier opponentJack() const;
    /**
     * This challenger's supporter chain at any depth: the direct supporter-jack
     * peer, which a cable proves, plus every device that named this one as its
     * challenger in a kChainJoin. Empty inside a ring, which has no challenger.
     */
    std::vector<std::array<uint8_t, 6>> getSupporterChainPeers() const;

    void sendGameEventToSupporters(ChainGameEventType eventType);
    void sendConfirm();

    /**
     * Re-sends the confirm this device is standing on to whichever challenger it
     * now holds. No-op until a press has produced one, so a topology event can
     * never register a supporter who never pressed.
     */
    void resendConfirm();

    /**
     * Supporter-side view of an inbound chain game event. A COUNTDOWN voids the
     * standing confirm: the challenger wipes its roll call at that same moment, so
     * re-sending the old press would register a supporter for a round it has not
     * yet answered.
     */
    void onChainGameEventReceived(uint8_t eventType);

    /**
     * One slot: the state watching for game events sets it on mount and clears
     * it on dismount. Told after onChainGameEventReceived has applied the event
     * and before the frame is acked.
     */
    void setOnGameEvent(GameEventListener listener) { onGameEvent = std::move(listener); }

    /** True when a broadcast game event names the challenger this device follows. */
    bool isEventFromOwnChallenger(const uint8_t* eventChallengerMac) const;

    /**
     * Challenger-side: records `supporterMac` as part of this device's chain, so
     * a press from a device it shares no cable with can be counted. Ignores a
     * join naming any challenger but this device.
     */
    void onChainJoinReceived(const uint8_t* supporterMac, const uint8_t* joinChallengerMac);
    /** Records a supporter press from originatorMac; chain membership is filtered when read. */
    void onConfirmReceived(const uint8_t* originatorMac);
    void onChainStateChanged();

    unsigned long getBoostMs() const;
    size_t getConfirmedSupporterCount() const;
    void clearSupporterConfirms();

    /** Returns the MAC of the Challenger this device follows, or nullptr if none. */
    const uint8_t* getChallengerMac() const;
    /** Updates the cached challenger from an opponent-jack role announce. */
    void onRoleAnnounceReceived(const uint8_t* fromMac, const uint8_t* challengerMac);

    /** Sends the cached Challenger MAC to the connected supporter-jack peer. */
    void broadcastRoleAndChallenger();

    void sync();

    static constexpr unsigned long BOOST_PER_SUPPORTER_MS = 15;
    /** The boost with every supporter slot confirmed: the most a draw can earn. */
    static constexpr unsigned long getMaxBoostMs() {
        return MAX_CHAIN_SUPPORTERS * BOOST_PER_SUPPORTER_MS;
    }
    /** Must exceed the retry span so the backstop does not supersede a live entry and reset its retry window. */
    static constexpr unsigned long ROLE_ANNOUNCE_BACKSTOP_MS = Resender::STALE_AFTER_MS;

    struct AckLatency {
        uint32_t sumMs = 0;
        uint32_t count = 0;
    };
    /** Delivery latency sampled on kRoleAnnounce (to SEND_SUCCESS) and kChainGameEvent (to its
     * ack). */
    AckLatency getAckLatency() const { return {ackLatencyMsSum, ackCount}; }

private:
    uint32_t ackLatencyMsSum = 0;
    uint32_t ackCount = 0;
    Player* player;
    WirelessManager* wirelessManager;
    RemoteDeviceCoordinator* rdc;

    SerialIdentifier supporterJack() const;

    // Tells the challenger this device follows that it exists. The challenger cannot
    // discover a supporter it shares no cable with any other way.
    void announceToChallenger();

    std::optional<bool> peerIsHunter(SerialIdentifier jack) const;

    // Ceiling shared by the roll call and the supporter roster: the longest
    // supporter chain a match is built for. Both buffers hold one MAC per
    // supporter, so one number bounds both.
    static constexpr size_t MAX_CHAIN_SUPPORTERS = 18;
    using MacSlots = std::array<std::array<uint8_t, 6>, MAX_CHAIN_SUPPORTERS>;

    // Appends `mac`, deduped. Full overwrites the oldest: refusing would let
    // any sender on the channel wedge the slots shut for the round.
    static void recordMac(MacSlots& slots, size_t& count, size_t& writeIndex, const uint8_t* mac);
    static bool containsMac(const MacSlots& slots, size_t count, const uint8_t* mac);

    size_t lastSupporterChainCount = 0;

    bool confirmSent = false;

    // Every press we have heard this round, member or not. Membership is applied
    // when the count is read, not here, because a confirm can beat the kChainJoin
    // that puts its originator in the roster and the sender gets no signal it was
    // dropped. Deciding late means an early confirm starts counting the moment
    // its join lands, and an unplugged supporter stops counting, with nothing to
    // re-offer or evict on a topology event.
    //
    // Fixed slots rather than a vector: packets append to this while a match is
    // running and the press path scans it to price the shot, so it must not
    // allocate, and the overwrite contract above needs a hard ceiling to be
    // expressible at all.
    MacSlots receivedConfirms{};
    size_t receivedConfirmCount = 0;
    size_t receivedConfirmWrite = 0;

    // Supporters that named this device as their challenger. Everything past the
    // direct supporter-jack peer lives here and nowhere else: the role cascade
    // only travels downstream, so a challenger has no way to see the far end of
    // its own chain until that end says so.
    MacSlots supporterRoster{};
    size_t supporterRosterCount = 0;
    size_t supporterRosterWrite = 0;

    std::optional<std::array<uint8_t, 6>> challengerMac;

    // Tracks whether the current (peer, challenger) pair was MAC-acked (SEND_SUCCESS).
    struct RoleAnnounceState {
        std::array<uint8_t, 6> peer{};
        std::array<uint8_t, 6> challenger{};
        uint8_t seqId = 0;
        bool delivered = false;

        /** True when this records a delivered announce of exactly `content`. */
        bool told(const RoleAnnounceState& content) const {
            return delivered && peer == content.peer && challenger == content.challenger;
        }
    };
    std::optional<RoleAnnounceState> supporterAnnounce;

    ReliableChannel<RoleAnnouncePayload> roleAnnounceChannel;
    ReliableChannel<ChainConfirmPayload> chainConfirmChannel;
    ReliableChannel<ChainJoinPayload> chainJoinChannel;
    FanOutChannel<ChainGameEventPayload> gameEventChannel;
    GameEventListener onGameEvent;

    void onGameEventFrame(const uint8_t* fromMac, const ChainGameEventPayload& payload);

    void recordAnnounceDelivered(uint8_t seqId, const uint8_t* mac);

    SimpleTimer roleAnnounceSentTimer;
    SimpleTimer gameEventSentTimer;
    SimpleTimer roleAnnounceBackstopTimer;
};
