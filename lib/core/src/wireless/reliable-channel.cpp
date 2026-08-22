#include "wireless/reliable-channel.hpp"
#include "device/wireless-manager.hpp"
#include "device/drivers/logger.hpp"

namespace {
constexpr const char* RELIABLE_CHANNEL_TAG = "ReliableChannel";
// Caps the per-channel RX dedup cursor table. The live peer set is bounded by
// the ESP-NOW peer cap, but senders come and go across a session and this table
// never otherwise shrinks; evicting the oldest cursor when full keeps it
// bounded. A wrongly-evicted still-active sender just re-seeds on its next
// packet, costing at most one re-dispatch that downstream domain dedup absorbs.
constexpr size_t MAX_RX_SENDERS = 32;
}

ReliableChannelBase::ReliableChannelBase(WirelessManager* wirelessManager,
                                         Resender* resender,
                                         PktType type,
                                         OnAbandon onAbandon,
                                         Resender::SendMode sendMode)
    : resender(resender)
    , packetType(type)
    , wirelessManager(wirelessManager)
    , sendMode(sendMode)
    , onAbandon(std::move(onAbandon)) {
    // Owning the PktType is the whole registration. This channel already holds
    // both things the wiring needs, and a channel whose send results never
    // arrive retries to exhaustion in silence — so leaving the install to
    // whoever constructs it makes a second, undeclared step that every caller
    // has to imitate and any caller can forget.
    if (this->wirelessManager == nullptr) return;
    this->wirelessManager->setEspNowSendStatusHandler(
        type,
        [](const uint8_t* dst, const uint8_t* data, const size_t len,
           bool success, void* ctx) {
            static_cast<ReliableChannelBase*>(ctx)->onSendResult(dst, data, len, success);
        },
        this);
}

ReliableChannelBase::~ReliableChannelBase() {
    // Last thing to run for this object, so the driver cannot dispatch into a
    // partly-destroyed channel afterwards.
    if (wirelessManager == nullptr) return;
    wirelessManager->clearEspNowSendStatusHandler(packetType);
}

void ReliableChannelBase::onAck(uint8_t seqId, const uint8_t* fromMac) {
    resender->onAck(packetType, seqId, fromMac);
}

void ReliableChannelBase::onResenderAbandon(uint8_t seqId, const uint8_t* targetMac) {
    if (onAbandon) onAbandon(seqId, targetMac);
}

bool ReliableChannelBase::isPending(const uint8_t* mac) const {
    return resender->isPending(packetType, mac);
}

void ReliableChannelBase::cancel(const uint8_t* mac) {
    resender->cancel(packetType, mac);
}

uint8_t ReliableChannelBase::nextSeqId() {
    // 0 is reserved for "no ack expected"; skip it on wrap.
    lastSentSeqId = lastSentSeqId == 255 ? 1 : lastSentSeqId + 1;
    return lastSentSeqId;
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

void ReliableChannelBase::sendOnceBytes(const uint8_t* mac, const uint8_t* data, size_t len) {
    WirelessManager* wm = getWirelessManager();
    if (wm == nullptr) {
        LOG_E(RELIABLE_CHANNEL_TAG, "sendOnceBytes called with null WirelessManager");
        return;
    }
    wm->sendEspNowData(mac, packetType, data, len);
}
