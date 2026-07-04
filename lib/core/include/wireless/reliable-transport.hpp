#pragma once

#include <cstdint>
#include <map>
#include <utility>
#include <vector>

#include "device/drivers/peer-comms-types.hpp"
#include "wireless/reliable-channel.hpp"
#include "wireless/resender.hpp"

class WirelessManager;

// Owns one Resender. Vends typed channels, one per PktType.
// All channels share the single Resender; abandon callbacks fire inline
// during sync() and must be cheap.
//
// Lifecycle of a reliable packet:
//   send: manager -> channel->sendReliable (serialize, stamp seqId) ->
//     Resender pending entry -> WirelessManager::sendEspNowData -> driver
//     (which itself retries a failed MAC-layer send). The platform loop's
//     transport->sync() retransmits on the RetryPolicy backoff until the ack
//     lands or retries exhaust -> the channel's abandon callback. Abandonment
//     is a game-level signal (void a match, abort a tournament), not a log
//     line; channel->cancel() drops pending sends WITHOUT it.
//   receive: driver rx -> per-PktType handler (installed here: the ack type at
//     construction, data types when their first channel is claimed) ->
//     deliverIncoming, which acks BEFORE dedup (a retransmit means our first
//     ack was lost: re-ack, then drop the duplicate) and dispatches the
//     decoded payload to the owning channel's onReceive. Claiming a channel
//     is sufficient to make its receive path live.
class ReliableTransport {
public:
    /// Installs the ack-packet handler on construction; wm may be nullptr in
    /// unit tests (channels then run without a radio).
    explicit ReliableTransport(WirelessManager* wm);

    /// Deletes every vended channel and rx binding.
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
                                ReliableChannelBase::OnAbandon onAbandon,
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
        ensurePacketCallback(type);
        return raw;
    }

    /// Routes the inbound AckPayload to the owning channel, if any.
    void onAckPacket(const uint8_t* from, const uint8_t* data, size_t len);

    /// Dispatches an inbound data packet to the channel claiming its PktType.
    /// Returns false if no channel is registered.
    bool deliverIncoming(PktType type, const uint8_t* fromMac,
                         const uint8_t* data, size_t len);

    /// Drives Resender retransmits and abandon dispatch. Called every loop
    /// tick by the platform loop only.
    void sync();

    /// nullptr in unit tests without a radio.
    WirelessManager* getWirelessManager() { return wirelessManager; }

private:
    // Installs the driver's per-PktType receive handler the first time a channel
    // claims that PktType. The driver callback carries only a void* ctx, so
    // each binding is a stable heap cell pairing this transport with the type.
    struct ReceiveBinding {
        ReliableTransport* transport;
        PktType type;
    };
    void ensurePacketCallback(PktType type);

    // Logs a PktType claimed by two different payload types (a wiring bug).
    static void logChannelTypeCollision(PktType type, size_t got, size_t have);

    void onResenderAbandon(PktType type, uint8_t seqId,
                           const uint8_t* targetMac);

    WirelessManager* wirelessManager;
    Resender resender;
    std::map<PktType, ReliableChannelBase*> registry;
    std::vector<ReceiveBinding*> receiveBindings;
};
