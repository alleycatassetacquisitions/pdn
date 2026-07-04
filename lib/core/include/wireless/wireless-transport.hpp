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
class WirelessTransport {
public:
    /// Installs the ack-packet handler on construction; wm may be nullptr in
    /// unit tests (channels then run without a radio).
    explicit WirelessTransport(WirelessManager* wm);

    /// Deletes every vended channel and rx binding.
    ~WirelessTransport();

    /// Constructs the channel owning a PktType. Aborts (loud failure) if
    /// another channel has already claimed it. The returned pointer stays
    /// owned by the transport.
    template <class P>
    ReliableChannel<P>* channel(PktType type,
                                ReliableChannelBase::OnAbandon onAbandon,
                                Resender::SendMode sendMode = Resender::SendMode::SUPERSEDE_PER_TARGET) {
        ReliableChannel<P>* raw = new ReliableChannel<P>(
            wirelessManager, &resender, type, std::move(onAbandon), sendMode);
        std::pair<std::map<PktType, ReliableChannelBase*>::iterator, bool> inserted =
            registry.insert({type, raw});
        if (!inserted.second) {
            abortWithMessage("duplicate channel claim");
        }
        ensureReceiveBinding(type);
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
        WirelessTransport* transport;
        PktType type;
    };
    void ensureReceiveBinding(PktType type);

    [[noreturn]] static void abortWithMessage(const char* msg);

    void onResenderAbandon(PktType type, uint8_t seqId,
                           const uint8_t* targetMac);

    WirelessManager* wirelessManager;
    Resender resender;
    std::map<PktType, ReliableChannelBase*> registry;
    std::vector<ReceiveBinding*> receiveBindings;
};
