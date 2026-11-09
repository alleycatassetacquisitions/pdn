#include "wireless/reliable-channel.hpp"
#include "wireless/reliable-transport.hpp"
#include "device/wireless-manager.hpp"
#include "device/drivers/logger.hpp"

#ifdef NATIVE_BUILD
#include <random>
#else
#include <esp_random.h>
#endif

namespace {
constexpr const char* RELIABLE_CHANNEL_TAG = "ReliableChannel";
constexpr size_t MAX_RX_SENDERS = 32;

uint8_t seedInitialSeqId() {
#ifdef NATIVE_BUILD
    const uint32_t raw = std::random_device{}();
#else
    const uint32_t raw = esp_random();
#endif
    return static_cast<uint8_t>(1 + (raw % 255));
}
}

ReliableChannelBase::ReliableChannelBase(WirelessManager* wirelessManager, PktType type,
                                         ChannelMode mode)
    : resender(nullptr)
    , packetType(type)
    , wirelessManager(wirelessManager)
    , channelMode(mode)
    , transport(wirelessManager != nullptr ? wirelessManager->getReliableTransport() : nullptr)
    , lastSentSeqId(seedInitialSeqId()) {
    // Owning the PktType is the whole registration. This channel already holds
    // both things the wiring needs, and a channel whose send results never
    // arrive retries to exhaustion in silence — so leaving the install to
    // whoever constructs it makes a second, undeclared step that every caller
    // has to imitate and any caller can forget.
    if (transport == nullptr) return;
    resender = &transport->resender;
    transport->attach(this);
    wirelessManager->setEspNowPacketHandler(
    type,
    [](const uint8_t* src, const uint8_t* data, const size_t len, void* ctx) {
        static_cast<ReliableChannelBase*>(ctx)->deliverBytes(src, data, len);
    },
    this);
    wirelessManager->setEspNowSendStatusHandler(
    type,
    [](const uint8_t* dst, const uint8_t* data, const size_t len, bool success, void* ctx) {
        static_cast<ReliableChannelBase*>(ctx)->onSendResult(dst, data, len, success);
    },
    this);
}

ReliableChannelBase::~ReliableChannelBase() {
    // A base destructor runs AFTER the derived one, so between the two the slot
    // still points at an object whose deliverBytes is pure virtual again. What
    // makes that safe is not the ordering here: the driver queues receives and
    // records a send verdict, acting on both from the main loop, which
    // is also where channels are destroyed, so no dispatch can land mid-teardown.
    if (transport == nullptr) return;
    resender->cancelAll(packetType);
    transport->detach(this);
    wirelessManager->clearEspNowPacketHandler(packetType);
    wirelessManager->clearEspNowSendStatusHandler(packetType);
}

bool ReliableChannelBase::isPending(const uint8_t* mac) const {
    return resender != nullptr && resender->isPending(packetType, mac);
}

size_t ReliableChannelBase::pendingCount(uint8_t seqId) const {
    return resender != nullptr ? resender->pendingCount(packetType, seqId) : 0;
}

void ReliableChannelBase::cancel(const uint8_t* mac) {
    if (resender != nullptr) resender->cancel(packetType, mac);
}

void ReliableChannelBase::cancelAll() {
    if (resender != nullptr) resender->cancelAll(packetType);
}

void ReliableChannelBase::cancelAllExcept(uint8_t keepSeqId) {
    if (resender != nullptr) resender->cancelAllExcept(packetType, keepSeqId);
}

void ReliableChannelBase::ack(const uint8_t* toMac, uint8_t seqId) {
    if (wirelessManager == nullptr || toMac == nullptr || seqId == 0) return;
    ReliableAckPayload ack{packetType, seqId};
    wirelessManager->sendEspNowData(toMac, PktType::kReliableAck,
                                    reinterpret_cast<const uint8_t*>(&ack), sizeof(ack));
}

void ReliableChannelBase::onPacketAck(const uint8_t* fromMac, uint8_t seqId) {
    if (resender != nullptr) resender->onAck(packetType, seqId, fromMac);
    fireDelivered(seqId, fromMac);
}

void ReliableChannelBase::fireDelivered(uint8_t seqId, const uint8_t* mac) {
    if (onDeliveredCallback) onDeliveredCallback(seqId, mac);
}

uint8_t ReliableChannelBase::nextSeqId() {
    // 0 is reserved for "no ack expected"; skip it on wrap.
    lastSentSeqId = lastSentSeqId == 255 ? 1 : lastSentSeqId + 1;
    return lastSentSeqId;
}

void ReliableChannelBase::sendUnsequencedBytes(const uint8_t* mac, const uint8_t* data,
                                               size_t len) {
    if (wirelessManager == nullptr || mac == nullptr) return;
    wirelessManager->sendEspNowData(mac, packetType, data, len);
}

bool ReliableChannelBase::isDuplicateReliableRx(const uint8_t* fromMac, uint8_t seqId) {
    if (seqId == 0 || fromMac == nullptr) return false;
    for (RxSeqRecord& r : rxSeq) {
        if (std::memcmp(r.mac.data(), fromMac, 6) == 0) {
            if (r.lastSeqId == seqId) return true;
            r.lastSeqId = seqId;
            return false;
        }
    }
    RxSeqRecord rec;
    std::memcpy(rec.mac.data(), fromMac, 6);
    rec.lastSeqId = seqId;
    if (rxSeq.size() >= MAX_RX_SENDERS) {
        rxSeq.erase(rxSeq.begin());
    }
    rxSeq.push_back(rec);
    return false;
}

void ReliableChannelBase::logLengthMismatch(PktType type, size_t got, size_t want) {
    LOG_W(RELIABLE_CHANNEL_TAG, "reliable rx len mismatch type=%u got=%zu want=%zu",
          (unsigned)type, got, want);
}
