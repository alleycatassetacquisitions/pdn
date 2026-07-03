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
    , onAbandon(std::move(onAbandon)) {}

void ReliableChannelBase::onAck(uint8_t seqId, const uint8_t* fromMac) {
    if (resender) {
        resender->onAck(packetType, seqId, fromMac);
    }
}

void ReliableChannelBase::onResenderAbandon(uint8_t seqId, const uint8_t* targetMac) {
    if (onAbandon) onAbandon(seqId, targetMac);
}

bool ReliableChannelBase::isPending(const uint8_t* mac) const {
    return resender && resender->isPending(packetType, mac);
}

void ReliableChannelBase::cancel(const uint8_t* mac) {
    if (resender) resender->cancel(packetType, mac);
}

uint8_t ReliableChannelBase::nextSeqId() {
    // 0 is reserved for "no ack expected"; skip it on wrap.
    lastSentSeqId = lastSentSeqId == 255 ? 1 : lastSentSeqId + 1;
    return lastSentSeqId;
}

const uint8_t* ReliableChannelBase::selfMac() const {
    WirelessManager* wm = getWirelessManager();
    return wm ? wm->getMacAddress() : nullptr;
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
