#include "wireless/wireless-transport.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "device/drivers/logger.hpp"
#include "device/wireless-manager.hpp"

namespace {
constexpr const char* WIRELESS_TRANSPORT_TAG = "WirelessTransport";
}

WirelessTransport::WirelessTransport(WirelessManager* wm)
    : wirelessManager(wm)
    , resender(wm) {
    resender.setAbandonCallback(
        [this](PktType type, uint8_t seqId, const uint8_t* targetMac) {
            onResenderAbandon(type, seqId, targetMac);
        });
    if (wirelessManager != nullptr) {
        wirelessManager->setEspNowPacketHandler(
            PktType::ACK,
            [](const uint8_t* src, const uint8_t* data, const size_t len,
               void* ctx) {
                static_cast<WirelessTransport*>(ctx)->onAckPacket(src, data, len);
            },
            this);
    }
}

WirelessTransport::~WirelessTransport() {
    for (std::pair<const PktType, ReliableChannelBase*>& entry : registry) {
        delete entry.second;
        entry.second = nullptr;
    }
    registry.clear();
    for (RxBinding*& binding : rxBindings) {
        delete binding;
        binding = nullptr;
    }
    rxBindings.clear();
}

void WirelessTransport::ensureRxBinding(PktType type) {
    for (const RxBinding* b : rxBindings) {
        if (b->type == type) return;
    }
    rxBindings.push_back(new RxBinding{this, type});
    if (wirelessManager == nullptr) return;
    wirelessManager->setEspNowPacketHandler(
        type,
        [](const uint8_t* src, const uint8_t* data, const size_t len,
           void* ctx) {
            RxBinding* b = static_cast<RxBinding*>(ctx);
            b->transport->deliverIncoming(b->type, src, data, len);
        },
        rxBindings.back());
}

void WirelessTransport::onAckPacket(const uint8_t* from,
                                    const uint8_t* data, size_t len) {
    if (len < sizeof(AckPayload)) return;
    AckPayload ack;
    std::memcpy(&ack, data, sizeof(ack));
    PktType origType = static_cast<PktType>(ack.originalType);
    std::map<PktType, ReliableChannelBase*>::iterator it = registry.find(origType);
    if (it == registry.end()) return;
    it->second->onAck(ack.seqId, from);
}

bool WirelessTransport::deliverIncoming(PktType type, const uint8_t* fromMac,
                                        const uint8_t* data, size_t len) {
    std::map<PktType, ReliableChannelBase*>::iterator it = registry.find(type);
    if (it == registry.end()) return false;
    return it->second->deliverBytes(fromMac, data, len);
}

void WirelessTransport::sync() {
    resender.sync();
}

void WirelessTransport::onResenderAbandon(PktType type, uint8_t seqId,
                                          const uint8_t* targetMac) {
    std::map<PktType, ReliableChannelBase*>::iterator it = registry.find(type);
    if (it == registry.end()) return;
    it->second->onResenderAbandon(seqId, targetMac);
}

void WirelessTransport::abortWithMessage(const char* msg) {
    LOG_E(WIRELESS_TRANSPORT_TAG, "%s", msg);
    // Also write to stderr so death-test matchers (which scan child stderr)
    // can detect the abort message in native unit-test builds.
    std::fprintf(stderr, "%s\n", msg);
    std::abort();
}
