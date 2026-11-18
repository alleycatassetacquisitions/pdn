#pragma once

#include <cstdint>
#include <map>
#include <utility>
#include <vector>

#include "device/drivers/peer-comms-types.hpp"
#include "wireless/reliable-channel.hpp"
#include "wireless/resender.hpp"

class WirelessManager;

// Owns one Resender. Vends typed channels, one per PktType, all sharing it.
// It is on neither the rx nor the send-result path: each channel installs its
// own driver handlers. channel->cancel() drops pending sends without the
// abandon callback.
class ReliableTransport {
public:
    /**
     * Routes Resender abandonment back to the owning channel; wm may be nullptr
     * in unit tests (channels then run without a radio).
     */
    explicit ReliableTransport(WirelessManager* wm);

    /** Deletes every vended channel; each drops its own driver handlers as it goes. */
    ~ReliableTransport();

    /**
     * Get-or-create the channel owning a PktType; the transport keeps ownership.
     * A re-claim with the same payload size returns the existing channel with its
     * abandon callback rebound; a different payload size logs and returns nullptr.
     */
    template <class P>
    ReliableChannel<P>* channel(PktType type,
                                ReliableChannelBase::OnAbandon onAbandon = {},
                                Resender::SendMode sendMode = Resender::SendMode::SUPERSEDE_PER_TARGET) {
        std::map<PktType, ReliableChannelBase*>::iterator it = registry.find(type);
        if (it != registry.end()) {
            if (it->second->payloadSize() != sizeof(P)) {
                logChannelTypeCollision(type, sizeof(P), it->second->payloadSize());
                return nullptr;
            }
            it->second->setOnAbandon(std::move(onAbandon));
            return static_cast<ReliableChannel<P>*>(it->second);
        }
        ReliableChannel<P>* raw = new ReliableChannel<P>(
            wirelessManager, &resender, type, std::move(onAbandon), sendMode);
        registry.insert({type, raw});
        return raw;
    }

    /**
     * Test seam: drives a radio send-result into the channel claiming this
     * PktType, for a caller holding a type rather than a channel handle.
     * Not on the driver's path — each channel installs its own send-status
     * handler and the driver calls it directly.
     */
    void onSendResult(PktType type, const uint8_t* toMac,
                      const uint8_t* data, size_t len, bool success);

    /**
     * Test seam, paired with onSendResult: dispatches an inbound packet to the
     * channel claiming this PktType. Returns false if no channel is registered.
     * Not on the driver's path — each channel installs its own packet handler.
     */
    bool deliverIncoming(PktType type, const uint8_t* fromMac,
                         const uint8_t* data, size_t len);

    /** Drives Resender retransmits and abandon dispatch; must run every tick. */
    void sync();

private:
    // Logs a PktType claimed with two different payload sizes (a wiring bug).
    static void logChannelTypeCollision(PktType type, size_t got, size_t have);

    void onResenderAbandon(PktType type, uint8_t seqId,
                           const uint8_t* targetMac);

    WirelessManager* wirelessManager;
    Resender resender;
    std::map<PktType, ReliableChannelBase*> registry;
};
