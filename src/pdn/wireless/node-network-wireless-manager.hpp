#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>

#include "device/drivers/peer-comms-types.hpp"
#include "device/wireless-manager.hpp"
#include "utils/uuid-bytes.hpp"

constexpr uint8_t NODE_NETWORK_PROTOCOL_VERSION = 1;
constexpr size_t NODE_NETWORK_PACKET_WIRE_SIZE = 67;

enum class NodeNetworkCommand : uint8_t {
    OFFER = 1,
    ACCEPT = 2,
    ROLE_MISMATCH = 3,
    TIME_CORRECTION = 4,
    DRAW = 5,
    NEVER_PRESSED = 6,
};

/** On-wire ESP-NOW layout; only plain scalars and byte arrays so packing is honored. */
struct NodeNetworkPacket {
    uint8_t protocolVersion = NODE_NETWORK_PROTOCOL_VERSION;
    uint8_t command = static_cast<uint8_t>(NodeNetworkCommand::OFFER);
    uint8_t senderIsInitiator = 0;
    uint8_t matchId[UUIDBytes::SIZE] = {};
    uint8_t senderPlayerId[UUIDBytes::SIZE] = {};
    uint8_t senderTeamId[UUIDBytes::SIZE] = {};
    uint64_t drawTimeMs = 0;
    int64_t occurredAtUnixMs = 0;
} __attribute__((packed));

static_assert(sizeof(NodeNetworkPacket) == NODE_NETWORK_PACKET_WIRE_SIZE,
              "NodeNetworkPacket wire size changed");
static_assert(offsetof(NodeNetworkPacket, matchId) == 3, "matchId wire offset");
static_assert(offsetof(NodeNetworkPacket, senderPlayerId) == 19, "senderPlayerId wire offset");
static_assert(offsetof(NodeNetworkPacket, senderTeamId) == 35, "senderTeamId wire offset");
static_assert(offsetof(NodeNetworkPacket, drawTimeMs) == 51, "drawTimeMs wire offset");
static_assert(offsetof(NodeNetworkPacket, occurredAtUnixMs) == 59, "occurredAtUnixMs wire offset");

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

/** Encodes, decodes, and validates NodeNetwork duel frames. Gating by peer or session is the caller's job. */
class NodeNetworkWirelessManager {
public:
    using PacketCallback = std::function<void(const uint8_t*, const NodeNetworkPacket&)>;

    virtual ~NodeNetworkWirelessManager() = default;

    /** Sets the transport used by the default `sendPacket`. */
    void initialize(WirelessManager* wirelessManager);

    /** Registers the single receiver of validated packets, replacing any earlier one. */
    void setPacketReceivedCallback(const PacketCallback& callback);

    /** Drops the registered receiver. */
    void clearCallbacks();

    /**
     * Validates a received frame and hands it to the callback.
     *
     * @return 1 when the frame was valid, -1 when it was dropped for length, version, or command.
     */
    int processPacket(const uint8_t* senderMac, const uint8_t* data, size_t dataLength);

    /** Sends @p packet to @p destinationMac over ESP-NOW. */
    virtual int sendPacket(const uint8_t* destinationMac, const NodeNetworkPacket& packet);

private:
    WirelessManager* wirelessManager = nullptr;
    PacketCallback packetReceivedCallback;
};
