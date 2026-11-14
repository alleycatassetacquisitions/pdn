#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>

#include "device/drivers/peer-comms-types.hpp"
#include "device/drivers/platform-clock.hpp"
#include "device/wireless-manager.hpp"
#include "utils/uuid-bytes.hpp"

constexpr size_t NODE_NETWORK_PACKET_WIRE_SIZE = 87;

/** Steps whose prompts one SEQUENCE carries. */
constexpr size_t NODE_NETWORK_PACKET_STEPS = 5;

/** Time between resends of an unacknowledged reliable packet; above the driver's own retries. */
constexpr unsigned long NODE_RELIABLE_RETRY_MS = 100;

/** Sends of one reliable packet (first send included) before it is reported as undelivered. */
constexpr uint8_t NODE_RELIABLE_MAX_ATTEMPTS = 8;

/** Reliable packets that may await an ACK at once. */
constexpr size_t NODE_RELIABLE_PENDING_CAPACITY = 16;

/** Recently delivered reliable packets remembered to drop repeats. */
constexpr size_t NODE_RELIABLE_SEEN_CAPACITY = 16;

enum class NodeNetworkCommand : uint8_t {
    OFFER = 1,
    ACCEPT = 2,
    ROLE_MISMATCH = 3,
    TIME_CORRECTION = 4,
    SYNC = 5,
    SYNC_REPLY = 6,
    SEQUENCE = 7,
    INPUT_RESULT = 8,
    OUTCOME = 9,
    ABORT = 10,
    ACK = 11,
    INPUT_PRESS = 12,
};

/**
 * On-wire ESP-NOW layout; only plain scalars and byte arrays so packing is honored.
 * Fields after senderTeamId are only meaningful for the commands named on them.
 */
struct NodeNetworkPacket {
    uint8_t command = static_cast<uint8_t>(NodeNetworkCommand::OFFER);
    uint8_t senderIsInitiator = 0;
    /** 1 when the receiver must answer with ACK. */
    uint8_t reliable = 0;
    /** Reliable packets: the sender's id for it. ACK: the id being acknowledged. */
    uint16_t sequenceNumber = 0;
    uint8_t matchId[UUIDBytes::SIZE] = {};
    uint8_t senderPlayerId[UUIDBytes::SIZE] = {};
    uint8_t senderTeamId[UUIDBytes::SIZE] = {};
    /** INPUT_RESULT and INPUT_PRESS. */
    uint8_t stepIndex = 0;
    /** INPUT_RESULT. */
    uint8_t inputSuccess = 0;
    /** INPUT_RESULT and INPUT_PRESS: buttons the sender has pressed in the step so far. */
    uint8_t pressCount = 0;
    /**
     * SEQUENCE: one byte per step, initiator's prompt in the low nibble and responder's in the
     * high nibble. Nibble bits 0-1 are the button count, bit 2 the first button and bit 3 the
     * second button (set means secondary).
     */
    uint8_t stepPrompts[NODE_NETWORK_PACKET_STEPS] = {};
    /** OUTCOME. */
    uint8_t initiatorScore = 0;
    /** OUTCOME. */
    uint8_t responderScore = 0;
    /** SYNC / SYNC_REPLY: initiator uptime when the probe left. */
    uint32_t syncOriginMs = 0;
    /** SYNC_REPLY: responder uptime when the probe arrived. */
    uint32_t syncReceiveMs = 0;
    /** SYNC_REPLY: responder uptime when the reply left. */
    uint32_t syncTransmitMs = 0;
    /** SEQUENCE: step 0 start in the receiver's uptime. */
    uint32_t stepZeroAtMs = 0;
    /** OFFER and TIME_CORRECTION. */
    int64_t occurredAtUnixMs = 0;
} __attribute__((packed));

static_assert(sizeof(NodeNetworkPacket) == NODE_NETWORK_PACKET_WIRE_SIZE,
              "NodeNetworkPacket wire size changed");
static_assert(offsetof(NodeNetworkPacket, sequenceNumber) == 3, "sequenceNumber wire offset");
static_assert(offsetof(NodeNetworkPacket, matchId) == 5, "matchId wire offset");
static_assert(offsetof(NodeNetworkPacket, senderPlayerId) == 21, "senderPlayerId wire offset");
static_assert(offsetof(NodeNetworkPacket, senderTeamId) == 37, "senderTeamId wire offset");
static_assert(offsetof(NodeNetworkPacket, stepIndex) == 53, "stepIndex wire offset");
static_assert(offsetof(NodeNetworkPacket, stepPrompts) == 56, "stepPrompts wire offset");
static_assert(offsetof(NodeNetworkPacket, syncOriginMs) == 63, "syncOriginMs wire offset");
static_assert(offsetof(NodeNetworkPacket, stepZeroAtMs) == 75, "stepZeroAtMs wire offset");
static_assert(offsetof(NodeNetworkPacket, occurredAtUnixMs) == 79, "occurredAtUnixMs wire offset");

inline void nodeNetworkPacketSetUuid(uint8_t (&dest)[UUIDBytes::SIZE], const UUIDBytes& uuid) {
    memcpy(dest, uuid.bytes, UUIDBytes::SIZE);
}

inline UUIDBytes nodeNetworkPacketGetUuid(const uint8_t (&src)[UUIDBytes::SIZE]) {
    UUIDBytes out;
    memcpy(out.bytes, src, UUIDBytes::SIZE);
    return out;
}

inline bool nodeNetworkPacketUuidIsZero(const uint8_t (&src)[UUIDBytes::SIZE]) {
    return nodeNetworkPacketGetUuid(src).isZero();
}

inline bool nodeNetworkPacketUuidEquals(const uint8_t (&left)[UUIDBytes::SIZE],
                                        const UUIDBytes& right) {
    return nodeNetworkPacketGetUuid(left) == right;
}

/**
 * Encodes, decodes, and validates NodeNetwork frames, and makes selected packets reliable
 * (ACK, resend, drop repeats). Gating by peer or session is the caller's job.
 */
class NodeNetworkWirelessManager {
public:
    using PacketCallback = std::function<void(const uint8_t*, const NodeNetworkPacket&)>;
    /** Destination MAC, the packet as sent, and whether an ACK arrived. */
    using DeliveryCallback =
    std::function<void(const uint8_t*, const NodeNetworkPacket&, bool delivered)>;

    virtual ~NodeNetworkWirelessManager() = default;

    /** Sets the transport used by the default `sendPacket` and the clock that paces resends. */
    void initialize(WirelessManager* wirelessManager, PlatformClock* platformClock);

    /** Sets the clock that paces resends; `initialize` also sets it. */
    void setClock(PlatformClock* platformClock);

    /** Registers the single receiver of validated packets, replacing any earlier one. */
    void setPacketReceivedCallback(const PacketCallback& callback);

    /** Registers the single receiver of reliable-delivery results, replacing any earlier one. */
    void setDeliveryCallback(const DeliveryCallback& callback);

    /** Drops the registered receivers. */
    void clearCallbacks();

    /**
     * Validates a received frame, acknowledges reliable ones, and hands new ones to the callback.
     * ACKs are consumed here.
     *
     * @return 1 when the frame was valid, -1 when it was dropped for length or command.
     */
    int processPacket(const uint8_t* senderMac, const uint8_t* data, size_t dataLength);

    /** Sends @p packet to @p destinationMac over ESP-NOW once. */
    virtual int sendPacket(const uint8_t* destinationMac, const NodeNetworkPacket& packet);

    /**
     * Sends @p packet and keeps resending it from `update` until it is acknowledged or
     * NODE_RELIABLE_MAX_ATTEMPTS sends have gone out.
     *
     * @return -1 when the pending table is full, otherwise the first send's result.
     */
    int sendReliable(const uint8_t* destinationMac, const NodeNetworkPacket& packet);

    /** Resends due reliable packets and reports the ones that ran out of attempts. */
    void update();

    /** @return reliable packets still waiting for an ACK. */
    size_t getPendingReliableCount() const;

private:
    struct PendingPacket {
        bool active = false;
        uint8_t destinationMac[6] = {};
        NodeNetworkPacket packet;
        uint8_t attempts = 0;
        unsigned long lastSentMs = 0;
    };

    struct SeenPacket {
        bool used = false;
        uint8_t senderMac[6] = {};
        uint8_t matchId[UUIDBytes::SIZE] = {};
        uint16_t sequenceNumber = 0;
    };

    unsigned long nowMs() const;
    void onAck(const uint8_t* senderMac, const NodeNetworkPacket& ack);
    void sendAck(const uint8_t* destinationMac, const NodeNetworkPacket& packet);
    /** @return true when the packet was seen before; otherwise remembers it. */
    bool rememberReliable(const uint8_t* senderMac, const NodeNetworkPacket& packet);

    WirelessManager* wirelessManager = nullptr;
    PlatformClock* platformClock = nullptr;
    PacketCallback packetReceivedCallback;
    DeliveryCallback deliveryCallback;
    PendingPacket pending[NODE_RELIABLE_PENDING_CAPACITY];
    SeenPacket seen[NODE_RELIABLE_SEEN_CAPACITY];
    size_t nextSeenSlot = 0;
    uint16_t nextSequenceNumber = 1;
};
