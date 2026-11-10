#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

#include "device/drivers/peer-comms-types.hpp"
#include "wireless/reliable-channel.hpp"
#include "wireless/resender.hpp"

class WirelessManager;

// Owns one Resender and the PktType -> channel registry. Vends typed channels,
// one per PktType, all sharing the Resender; the caller owns each channel, and a
// channel detaches itself from the registry when destroyed.
//
// Lifecycle of a reliable packet:
//   send: manager -> channel->sendReliable (serialize, allocate seqId) ->
//     Resender pending entry -> WirelessManager::sendEspNowData -> driver
//     (which itself retries a failed MAC-layer send). The platform loop's
//     transport->sync() retransmits on the Resender's backoff until the radio
//     lands or the frame's span runs out -> the channel's abandon callback.
//     Abandonment is a game-level signal (void a match, abort a tournament), not
//     a log line; channel->cancel() drops pending sends WITHOUT it.
//   receive: driver rx -> the owning channel's own per-PktType handler, installed
//     by its constructor, straight into deliver, which dedups and dispatches the
//     decoded payload to onReceive. The transport is not on that path. No ack is
//     emitted anywhere on it either: a send is cleared by the radio's
//     SEND_SUCCESS, not a reply. Claiming a channel is sufficient to make its
//     receive path live.
class ReliableTransport {
public:
    /**
     * Routes Resender abandonment back to the owning channel; wm may be nullptr
     * in unit tests (channels then run without a radio).
     */
    explicit ReliableTransport(WirelessManager* wm);

    /**
     * Creates and registers the channel owning a PktType; the caller owns it.
     * A second claim of a claimed PktType is a wiring collision — two
     * subsystems fighting over one type — and returns nullptr after logging,
     * leaving the first channel in place.
     */
    template <class P>
    ReliableChannel<P>* channel(PktType type,
                                ReliableChannelBase::OnAbandon onAbandon = {},
                                Resender::SendMode sendMode = Resender::SendMode::SUPERSEDE_PER_TARGET) {
        if (channelFor(type) != nullptr) {
            logClaimRefused(type);
            return nullptr;
        }
        ReliableChannel<P>* raw = new ReliableChannel<P>(
            wirelessManager, &resender, type, std::move(onAbandon), sendMode);
        attach(raw);
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
     * Not on the driver's path — see the receive note above.
     */
    bool deliverIncoming(PktType type, const uint8_t* fromMac,
                         const uint8_t* data, size_t len);

    /**
     * Drives Resender retransmits and abandon dispatch. Called every loop
     * tick by the platform loop only.
     */
    void sync();

private:
    friend class ReliableChannelBase;

    static void logClaimRefused(PktType type);
    void attach(ReliableChannelBase* channel);
    void detach(ReliableChannelBase* channel);
    ReliableChannelBase* channelFor(PktType type) const;

    void onResenderAbandon(PktType type, uint8_t seqId,
                           const uint8_t* targetMac);

    WirelessManager* wirelessManager;
    Resender resender;
    std::array<ReliableChannelBase*, static_cast<size_t>(PktType::kNumPacketTypes)> channels{};
};
