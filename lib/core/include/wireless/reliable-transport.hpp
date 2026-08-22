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
//
// Lifecycle of a reliable packet:
//   send: manager -> channel->sendReliable (serialize, stamp seqId) ->
//     Resender pending entry -> WirelessManager::sendEspNowData -> driver
//     (which itself retries a failed MAC-layer send). The platform loop's
//     transport->sync() retransmits on the Resender's backoff until the radio
//     lands or retries exhaust -> the channel's abandon callback. Abandonment
//     is a game-level signal (void a match, abort a tournament), not a log
//     line; channel->cancel() drops pending sends WITHOUT it.
//   receive: driver rx -> the owning channel's own per-PktType handler, installed
//     by its constructor, straight into deliver, which dedups and dispatches the
//     decoded payload to onReceive. The transport is not on that path. No ack is
//     emitted anywhere on it either: a send is cleared by the radio's
//     SEND_SUCCESS, not a reply. Claiming a channel is sufficient to make its
//     receive path live.
class ReliableTransport {
public:
    /// Routes Resender abandonment back to the owning channel; wm may be nullptr
    /// in unit tests (channels then run without a radio).
    explicit ReliableTransport(WirelessManager* wm);

    /// Deletes every vended channel; each drops its own driver handlers as it goes.
    ~ReliableTransport();

    /// Get-or-create the channel owning a PktType. A first claim creates and
    /// registers it. A re-claim of the same PktType with the same payload type
    /// (e.g. a re-created owner re-initializing) returns the existing channel
    /// with its abandon callback rebound, so the caller just re-sets onReceive.
    /// A re-claim with a DIFFERENT payload type is a wiring collision — two
    /// subsystems fighting over one PktType — and returns nullptr after logging.
    /// The returned pointer stays owned by the transport.
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

    /// Test seam: drives a radio send-result into the channel claiming this
    /// PktType, for a caller holding a type rather than a channel handle.
    /// Not on the driver's path — each channel installs its own send-status
    /// handler and the driver calls it directly.
    void onSendResult(PktType type, const uint8_t* toMac,
                      const uint8_t* data, size_t len, bool success);

    /// Test seam, paired with onSendResult: dispatches an inbound packet to the
    /// channel claiming this PktType. Returns false if no channel is registered.
    /// Not on the driver's path — see the receive note above.
    bool deliverIncoming(PktType type, const uint8_t* fromMac,
                         const uint8_t* data, size_t len);

    /// Drives Resender retransmits and abandon dispatch. Called every loop
    /// tick by the platform loop only.
    void sync();

private:
    // Logs a PktType claimed by two different payload types (a wiring bug).
    static void logChannelTypeCollision(PktType type, size_t got, size_t have);

    void onResenderAbandon(PktType type, uint8_t seqId,
                           const uint8_t* targetMac);

    WirelessManager* wirelessManager;
    Resender resender;
    std::map<PktType, ReliableChannelBase*> registry;
};
