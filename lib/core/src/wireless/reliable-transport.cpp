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
        [this](PktType type, uint8_t seqId, const uint8_t* targetMac) {
            onResenderAbandon(type, seqId, targetMac);
        });
}

ReliableTransport::~ReliableTransport() {
    for (std::pair<const PktType, ReliableChannelBase*>& entry : registry) {
        delete entry.second;
        entry.second = nullptr;
    }
    registry.clear();
    // Unregister the driver callbacks before freeing their ctx cells: each
    // ReceiveBinding is the void* ctx held by the driver's per-type receive and
    // send-status handlers, so a packet arriving after this dtor would otherwise
    // dispatch into freed memory.
    for (ReceiveBinding*& binding : receiveBindings) {
        if (wirelessManager != nullptr) {
            wirelessManager->clearEspNowPacketHandler(binding->type);
            wirelessManager->clearEspNowSendStatusHandler(binding->type);
        }
        delete binding;
        binding = nullptr;
    }
    receiveBindings.clear();
}

void ReliableTransport::ensurePacketCallback(PktType type) {
    // Only ever called on a channel's first claim of `type` (channel() returns
    // early on a re-claim), so the binding is always new — no dedup needed.
    receiveBindings.push_back(new ReceiveBinding{this, type});
    ReceiveBinding* binding = receiveBindings.back();
    if (wirelessManager == nullptr) return;
    wirelessManager->setEspNowPacketHandler(
        type,
        [](const uint8_t* src, const uint8_t* data, const size_t len,
           void* ctx) {
            ReceiveBinding* b = static_cast<ReceiveBinding*>(ctx);
            b->transport->deliverIncoming(b->type, src, data, len);
        },
        binding);
    wirelessManager->setEspNowSendStatusHandler(
        type,
        [](const uint8_t* dst, const uint8_t* data, const size_t len,
           bool success, void* ctx) {
            ReceiveBinding* b = static_cast<ReceiveBinding*>(ctx);
            b->transport->onSendResult(b->type, dst, data, len, success);
        },
        binding);
}

void ReliableTransport::onSendResult(PktType type, const uint8_t* toMac,
                                     const uint8_t* data, size_t len,
                                     bool success) {
    std::map<PktType, ReliableChannelBase*>::iterator it = registry.find(type);
    if (it == registry.end()) return;
    it->second->onSendResult(toMac, data, len, success);
}

bool ReliableTransport::deliverIncoming(PktType type, const uint8_t* fromMac,
                                        const uint8_t* data, size_t len) {
    std::map<PktType, ReliableChannelBase*>::iterator it = registry.find(type);
    if (it == registry.end()) return false;
    return it->second->deliverBytes(fromMac, data, len);
}

void ReliableTransport::sync() {
    resender.sync();
}

void ReliableTransport::onResenderAbandon(PktType type, uint8_t seqId,
                                          const uint8_t* targetMac) {
    std::map<PktType, ReliableChannelBase*>::iterator it = registry.find(type);
    if (it == registry.end()) return;
    it->second->onResenderAbandon(seqId, targetMac);
}

void ReliableTransport::logChannelTypeCollision(PktType type, size_t got, size_t have) {
    LOG_E(RELIABLE_TRANSPORT_TAG,
          "channel(): PktType %u already claimed by a %zu-byte payload; "
          "rejected a %zu-byte claim (two subsystems on one PktType)",
          (unsigned)type, have, got);
}
