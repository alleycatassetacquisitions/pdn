#pragma once

#include <cstdint>
#include <functional>

#include "device/drivers/peer-comms-types.hpp"
#include "device/wireless-manager.hpp"
#include "utils/uuid-bytes.hpp"

constexpr uint8_t NODE_NETWORK_PROTOCOL_VERSION = 1;

enum class NodeNetworkCommand : uint8_t {
    OFFER = 1,
    ACCEPT = 2,
    ROLE_MISMATCH = 3,
    TIME_CORRECTION = 4,
    DRAW = 5,
    NEVER_PRESSED = 6,
};

/** Wire format for every NodeNetwork duel message. Fields a command does not use are zero. */
struct NodeNetworkPacket {
    uint8_t protocolVersion = NODE_NETWORK_PROTOCOL_VERSION;
    NodeNetworkCommand command = NodeNetworkCommand::OFFER;
    uint8_t senderIsInitiator = 0;
    UUIDBytes matchId;
    UUIDBytes senderPlayerId;
    UUIDBytes senderTeamId;
    uint64_t drawTimeMs = 0;
    int64_t occurredAtUnixMs = 0;
} __attribute__((packed));

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
