#include "wireless/reliable-transport.hpp"

#include "device/drivers/logger.hpp"
#include "wireless/reliable-channel.hpp"

namespace {
constexpr const char* RELIABLE_TRANSPORT_TAG = "ReliableTransport";
}

ReliableTransport::ReliableTransport(WirelessManager* wm)
    : resender(wm) {
    resender.setAbandonCallback([this](PktType type, uint8_t seqId, const uint8_t* targetMac,
                                       const uint8_t*,
                                       size_t) { onResenderAbandon(type, seqId, targetMac); });
}

void ReliableTransport::sync() { resender.sync(); }

bool ReliableTransport::attach(ReliableChannelBase* channel) {
    const size_t slot = static_cast<size_t>(channel->type());
    if (slot >= channels.size()) return false;
    if (channels[slot] != nullptr) {
        LOG_E(RELIABLE_TRANSPORT_TAG,
              "PktType %u already claimed; second channel runs without a radio",
              (unsigned)channel->type());
        return false;
    }
    channels[slot] = channel;
    return true;
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

void ReliableTransport::onResenderAbandon(PktType type, uint8_t seqId,
                                          const uint8_t* targetMac) {
    ReliableChannelBase* channel = channelFor(type);
    if (channel != nullptr) channel->onResenderAbandon(seqId, targetMac);
}
