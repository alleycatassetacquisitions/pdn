#include "wireless/reliable-transport.hpp"

#include <cstring>

#include "device/drivers/logger.hpp"
#include "device/wireless-manager.hpp"
#include "wireless/reliable-channel.hpp"

namespace {
constexpr const char* RELIABLE_TRANSPORT_TAG = "ReliableTransport";
}

ReliableTransport::ReliableTransport(WirelessManager* wm)
    : wirelessManager(wm)
    , resender(wm) {
    resender.setAbandonCallback(
    [this](PktType type, uint8_t seqId, const uint8_t* targetMac, const uint8_t* payload,
           size_t len) { onResenderAbandon(type, seqId, targetMac, payload, len); });
    wirelessManager->setEspNowPacketHandler(
    PktType::kReliableAck,
    [](const uint8_t* src, const uint8_t* data, const size_t len, void* ctx) {
        static_cast<ReliableTransport*>(ctx)->onAckPacket(src, data, len);
    },
    this);
}

ReliableTransport::~ReliableTransport() {
    wirelessManager->clearEspNowPacketHandler(PktType::kReliableAck);
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
    if (type == PktType::kReliableAck) {
        onAckPacket(fromMac, data, len);
        return true;
    }
    ReliableChannelBase* channel = channelFor(type);
    return channel != nullptr && channel->deliverBytes(fromMac, data, len);
}

void ReliableTransport::onAckPacket(const uint8_t* fromMac, const uint8_t* data, size_t len) {
    if (len != sizeof(ReliableAckPayload)) return;
    ReliableAckPayload ack;
    memcpy(&ack, data, sizeof(ack));
    ReliableChannelBase* channel = channelFor(ack.ackedType);
    if (channel != nullptr) channel->onPacketAck(fromMac, ack.seqId);
}

void ReliableTransport::onResenderAbandon(PktType type, uint8_t seqId, const uint8_t* targetMac,
                                          const uint8_t* payload, size_t len) {
    ReliableChannelBase* channel = channelFor(type);
    if (channel != nullptr) channel->onAbandon(seqId, targetMac, payload, len);
}
