#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <utility>
#include <vector>

#include "device/drivers/peer-comms-types.hpp"
#include "wireless/reliable-channel.hpp"
#include "wireless/resender.hpp"

class WirelessManager;

// Owns one Resender. Vends typed channels keyed by (PktType, subType).
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
//   receive: driver rx -> per-PktType handler (installed here: kAck at
//     construction, data types when their first channel is claimed) ->
//     deliverIncoming, which acks BEFORE dedup (a retransmit means our first
//     ack was lost: re-ack, then drop the duplicate) and dispatches the
//     decoded payload to the owning channel's onReceive. Claiming a channel
//     is sufficient to make its receive path live.
class WirelessTransport {
public:
    using Stats = Resender::Stats;

    explicit WirelessTransport(WirelessManager* wm);

    // Construct a channel for (PktType, subType=0). Aborts (loud failure)
    // if another channel has already claimed this (PktType, subType).
    template <class P>
    ReliableChannel<P>* channel(PktType type,
                                ReliableChannelBase::OnAbandon onAbandon,
                                Resender::SendMode sendMode =
                                    Resender::SendMode::SupersedePerTarget) {
        return channelImpl<P, void>(type, 0, std::move(onAbandon), sendMode);
    }

    // Construct a channel for (PktType, subType). Sub may be an enum class
    // (e.g., ShootoutCmd); its underlying uint8_t value is the subType.
    template <class P, class Sub>
    ReliableChannel<P, Sub>* channel(PktType type, Sub subType,
                                     ReliableChannelBase::OnAbandon onAbandon,
                                     Resender::SendMode sendMode =
                                         Resender::SendMode::SupersedePerTarget) {
        return channelImpl<P, Sub>(type, static_cast<uint8_t>(subType),
                                   std::move(onAbandon), sendMode);
    }

    // Routes the inbound AckPayload to the owning channel, if any.
    void onAckPacket(const uint8_t* from, const uint8_t* data, size_t len);

    // Dispatches an inbound data packet to the channel claiming
    // (type, subType). Returns false if no channel is registered.
    bool deliverIncoming(PktType type, uint8_t subType,
                         const uint8_t* fromMac, const uint8_t* data, size_t len);

    // Called every loop tick. Drives Resender's retransmits and abandon
    // dispatch.
    void sync();

    Stats getStats(PktType type, uint8_t subType = 0) {
        return resender_.getStats(type, subType);
    }

    WirelessManager* getWirelessManager() { return wm_; }

private:
    template <class P, class Sub>
    ReliableChannel<P, Sub>* channelImpl(PktType type, uint8_t subType,
                                         ReliableChannelBase::OnAbandon onAbandon,
                                         Resender::SendMode sendMode) {
        ChannelKey k = channelKey(type, subType);
        auto channel = std::make_unique<ReliableChannel<P, Sub>>(
            this, &resender_, type, subType, std::move(onAbandon), sendMode);
        auto* raw = channel.get();
        auto [it, inserted] = registry_.emplace(k, std::move(channel));
        if (!inserted) {
            abortWithMessage("duplicate channel claim");
        }
        ensureRxBinding(type);
        return raw;
    }

    // Installs the driver's per-PktType rx handler the first time a channel
    // claims that PktType. The driver callback carries only a void* ctx, so
    // each binding is a stable heap cell pairing this transport with the type.
    struct RxBinding {
        WirelessTransport* transport;
        PktType type;
    };
    void ensureRxBinding(PktType type);
    void dispatchInbound(PktType type, const uint8_t* fromMac,
                         const uint8_t* data, size_t len);

    [[noreturn]] static void abortWithMessage(const char* msg);

    void onResenderAbandon(PktType type, uint8_t subType,
                           uint8_t seqId, const uint8_t* targetMac);

    WirelessManager* wm_;
    Resender resender_;
    std::map<ChannelKey, std::unique_ptr<ReliableChannelBase>> registry_;
    std::vector<std::unique_ptr<RxBinding>> rxBindings_;
};
