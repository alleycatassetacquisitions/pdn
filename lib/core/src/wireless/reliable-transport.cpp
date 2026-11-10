#include "wireless/reliable-transport.hpp"

#include <cstring>

#include "device/drivers/logger.hpp"
#include "device/wireless-manager.hpp"

namespace {
constexpr const char* RELIABLE_TRANSPORT_TAG = "ReliableTransport";
}

ReliableTransport::ReliableTransport(WirelessManager* wm)
    : wirelessManager(wm)
    , resender(wm) {
    resender.setAbandonCallback(
        [this](PktType type, uint8_t seqId, const uint8_t* targetMac,
               const uint8_t*, size_t) {
            onResenderAbandon(type, seqId, targetMac);
        });
}

void ReliableTransport::attach(ReliableChannelBase* channel) {
    const size_t slot = static_cast<size_t>(channel->type());
    if (slot < channels.size()) {
        channels[slot] = channel;
        channel->transport = this;
    }
}

void ReliableTransport::detach(ReliableChannelBase* channel) {
    const size_t slot = static_cast<size_t>(channel->type());
    if (slot < channels.size() && channels[slot] == channel) channels[slot] = nullptr;
}

ReliableChannelBase* ReliableTransport::channelFor(PktType type) const {
    const size_t slot = static_cast<size_t>(type);
    return slot < channels.size() ? channels[slot] : nullptr;
}

void ReliableTransport::onSendResult(PktType type, const uint8_t* toMac,
                                     const uint8_t* data, size_t len,
                                     bool success) {
    ReliableChannelBase* channel = channelFor(type);
    if (channel != nullptr) channel->onSendResult(toMac, data, len, success);
}

bool ReliableTransport::deliverIncoming(PktType type, const uint8_t* fromMac,
                                        const uint8_t* data, size_t len) {
    ReliableChannelBase* channel = channelFor(type);
    return channel != nullptr && channel->deliverBytes(fromMac, data, len);
}

void ReliableTransport::sync() {
    resender.sync();
}

void ReliableTransport::onResenderAbandon(PktType type, uint8_t seqId,
                                          const uint8_t* targetMac) {
    ReliableChannelBase* channel = channelFor(type);
    if (channel != nullptr) channel->onResenderAbandon(seqId, targetMac);
}

void ReliableTransport::logClaimRefused(PktType type) {
    LOG_E(RELIABLE_TRANSPORT_TAG, "PktType %u already claimed; second claim refused",
          (unsigned)type);
}
