#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "device/drivers/peer-comms-types.hpp"
#include "wireless/resender.hpp"

class ReliableChannelBase;
class WirelessManager;

// The device's one reliable transport: owns the one Resender every channel
// shares and the PktType -> channel registry that routes what the Resender
// cannot address by itself — abandonment, and the kReliableAck a fan-out
// recipient answers with. The WirelessManager owns one and pumps it from the
// device loop; channels attach to it when constructed and detach when destroyed.
//
// Receive and SEND_SUCCESS bypass the transport: each channel installs its own
// driver handlers. Only abandonment and kReliableAck route through here.
class ReliableTransport {
public:
    /** Built by the WirelessManager that owns it; registers the kReliableAck handler through wm. */
    explicit ReliableTransport(WirelessManager* wm);
    /** Drops the kReliableAck handler. Channels must be destroyed first: their destructors detach
     * through this object. */
    ~ReliableTransport();

    /** Drives Resender retransmits and abandon dispatch. */
    void sync();

    /** Cumulative send/retry/abandon counters across every channel. */
    const Resender::Stats& getStats() const { return resender.getStats(); }

    /**
     * Test seam: recipients of the frame sent on `type` under `seqId` that
     * still owe an ack. Channels gate on their own pendingCount.
     */
    size_t pendingCount(PktType type, uint8_t seqId) const {
        return resender.pendingCount(type, seqId);
    }

    /**
     * Test seam: drives a radio send-result into the channel claiming this
     * PktType, for a caller holding a type rather than a channel handle. Not
     * on the driver's path — each channel installs its own send-status handler
     * and the driver calls it directly.
     */
    void onSendResult(PktType type, const uint8_t* toMac,
                      const uint8_t* data, size_t len, bool success);

    /**
     * Test seam, paired with onSendResult: dispatches an inbound packet to the
     * channel claiming this PktType, or a kReliableAck to the channel it
     * answers. Returns false if no channel claims `type`; a kReliableAck
     * always returns true.
     */
    bool deliverIncoming(PktType type, const uint8_t* fromMac,
                         const uint8_t* data, size_t len);

private:
    friend class ReliableChannelBase;

    // One channel per PktType. A second channel on a claimed type is a wiring
    // bug: attach refuses it (logged), and that channel then runs without a
    // radio, exactly as one built on a null WirelessManager does.
    bool attach(ReliableChannelBase* channel);
    void detach(ReliableChannelBase* channel);
    ReliableChannelBase* channelFor(PktType type) const;

    void onAckPacket(const uint8_t* fromMac, const uint8_t* data, size_t len);
    void onResenderAbandon(PktType type, uint8_t seqId, const uint8_t* targetMac,
                           const uint8_t* payload, size_t len);

    WirelessManager* wirelessManager;
    Resender resender;
    std::array<ReliableChannelBase*, static_cast<size_t>(PktType::kNumPacketTypes)> channels{};
};
