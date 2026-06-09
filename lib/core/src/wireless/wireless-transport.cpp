#include "wireless/wireless-transport.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "device/drivers/logger.hpp"
#include "device/wireless-manager.hpp"

namespace {
const char* TAG = "WTX";
}

WirelessTransport::WirelessTransport(WirelessManager* wm)
    : wm_(wm), resender_(wm) {
    resender_.setAbandonCallback(
        [this](PktType type, uint8_t subType, uint8_t seqId,
               const uint8_t* targetMac) {
            onResenderAbandon(type, subType, seqId, targetMac);
        });
    if (wm_ != nullptr) {
        wm_->setEspNowPacketHandler(
            PktType::kAck,
            [](const uint8_t* src, const uint8_t* data, const size_t len,
               void* ctx) {
                static_cast<WirelessTransport*>(ctx)->onAckPacket(src, data, len);
            },
            this);
    }
}

void WirelessTransport::ensureRxBinding(PktType type) {
    for (const auto& b : rxBindings_) {
        if (b->type == type) return;
    }
    rxBindings_.push_back(std::make_unique<RxBinding>(RxBinding{this, type}));
    if (wm_ == nullptr) return;
    wm_->setEspNowPacketHandler(
        type,
        [](const uint8_t* src, const uint8_t* data, const size_t len,
           void* ctx) {
            auto* b = static_cast<RxBinding*>(ctx);
            b->transport->dispatchInbound(b->type, src, data, len);
        },
        rxBindings_.back().get());
}

void WirelessTransport::dispatchInbound(PktType type, const uint8_t* fromMac,
                                        const uint8_t* data, size_t len) {
    // Sub-typed payloads carry their demux key in byte 0 (stamped by the
    // sending channel). A PktType with a single subType-0 channel has
    // arbitrary data in byte 0, so resolve against claimed channels and fall
    // back to subType 0 when byte 0 matches none.
    uint8_t subType = 0;
    if (len >= 1 && registry_.count(channelKey(type, data[0])) != 0) {
        subType = data[0];
    }
    deliverIncoming(type, subType, fromMac, data, len);
}

void WirelessTransport::onAckPacket(const uint8_t* from,
                                    const uint8_t* data, size_t len) {
    if (len < sizeof(AckPayload)) return;
    AckPayload ack;
    std::memcpy(&ack, data, sizeof(ack));
    PktType origType = static_cast<PktType>(ack.originalType);
    ChannelKey k = channelKey(origType, ack.subType);
    auto it = registry_.find(k);
    if (it == registry_.end()) return;
    it->second->onAck(ack.seqId, from);
}

bool WirelessTransport::deliverIncoming(PktType type, uint8_t subType,
                                        const uint8_t* fromMac,
                                        const uint8_t* data, size_t len) {
    ChannelKey k = channelKey(type, subType);
    auto it = registry_.find(k);
    if (it == registry_.end()) return false;
    return it->second->deliverBytes(fromMac, data, len);
}

void WirelessTransport::sync() {
    resender_.sync();
}

void WirelessTransport::onResenderAbandon(PktType type, uint8_t subType,
                                          uint8_t seqId,
                                          const uint8_t* targetMac) {
    ChannelKey k = channelKey(type, subType);
    auto it = registry_.find(k);
    if (it == registry_.end()) return;
    it->second->onResenderAbandon(seqId, targetMac);
}

void WirelessTransport::abortWithMessage(const char* msg) {
    LOG_E(TAG, "%s", msg);
    // Also write to stderr so death-test matchers (which scan child stderr)
    // can detect the abort message in native unit-test builds.
    std::fprintf(stderr, "%s\n", msg);
    std::abort();
}
