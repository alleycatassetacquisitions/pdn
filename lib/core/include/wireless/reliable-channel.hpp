#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <vector>

#include "device/drivers/peer-comms-types.hpp"
#include "wireless/resender.hpp"

// A ReliableChannel is a typed, reliable pipe for exactly one PktType: game
// code sends a packed payload struct and receives decoded structs back, and
// the channel supplies everything reliability needs underneath — seqId
// stamping, retry-until-ack via the shared Resender, ack emission on receipt,
// duplicate suppression, and an abandon callback when a peer stays
// unreachable past the retry budget. The struct itself is the wire format
// (packed, memcpy'd); both ends run the same firmware, so field layout is the
// protocol. ReliableChannelBase holds the untyped mechanics; the
// ReliableChannel<P> template below binds them to a payload type. Channels
// are created and owned by ReliableTransport, one per PktType.
class ReliableChannelBase {
public:
    using OnAbandon = std::function<void(uint8_t seqId, const uint8_t* targetMac)>;

    /// wirelessManager may be nullptr in probe-style unit tests; every use is
    /// null-tolerant.
    ReliableChannelBase(WirelessManager* wirelessManager,
                        Resender* resender,
                        PktType type,
                        OnAbandon onAbandon,
                        Resender::SendMode sendMode = Resender::SendMode::SUPERSEDE_PER_TARGET);
    /// Virtual: channels are owned and deleted through the base pointer.
    virtual ~ReliableChannelBase() = default;

    /// The PktType this channel claims.
    PktType type() const { return packetType; }

    /// Routes an ack for this channel's PktType into the Resender.
    void onAck(uint8_t seqId, const uint8_t* fromMac);

    /// Relays a Resender abandonment to this channel's OnAbandon callback.
    void onResenderAbandon(uint8_t seqId, const uint8_t* targetMac);

    /// True when at least one send to this MAC is still awaiting its ack.
    bool isPending(const uint8_t* mac) const;

    /// Silently drops every pending send to this MAC (no abandon callback).
    void cancel(const uint8_t* mac);

    /// Typed subclass deserializes bytes into its payload type and dispatches
    /// to its onReceive callback. Returns true if delivered.
    virtual bool deliverBytes(const uint8_t* fromMac, const uint8_t* data, size_t len) = 0;

    /// Radio send-result for one of this channel's outbound packets. The typed
    /// subclass reads the stamped seqId back from `data` and, on success,
    /// clears the pending entry (replacing the old ack round-trip).
    virtual void onSendResult(const uint8_t* toMac, const uint8_t* data,
                              size_t len, bool success) = 0;

    /// sizeof the payload struct this channel is typed on. The transport uses
    /// it to tell a same-type re-claim from a different-payload collision on
    /// the same PktType.
    virtual size_t payloadSize() const = 0;

    /// Rebinds the abandon callback; used when a re-created owner re-claims an
    /// already-registered channel so the callback points at the live instance.
    void setOnAbandon(OnAbandon cb) { onAbandon = std::move(cb); }

protected:
    uint8_t nextSeqId();
    // nullptr in probe-style unit tests; every use is null-tolerant.
    WirelessManager* getWirelessManager() const { return wirelessManager; }
    void sendOnceBytes(const uint8_t* mac, const uint8_t* data, size_t len);
    // Defined in the .cpp so the logger stays out of this template header.
    static void logLengthMismatch(PktType type, size_t got, size_t want);
    // Suppress re-dispatch of a duplicate reliable delivery from the same
    // sender, the expected consequence of a lost ack on a resent packet.
    // seqId==0 is the unsequenced/sendOnce sentinel (see nextSeqId) and is
    // never deduped here; those payloads dedup by domain identity at the
    // caller. Returns true if (fromMac,seqId) was already delivered. Call only
    // AFTER acking, so a duplicate still silences the sender's resends.
    bool isDuplicateReliableRx(const uint8_t* fromMac, uint8_t seqId);

    Resender* resender;
    PktType packetType;
    WirelessManager* wirelessManager;
    Resender::SendMode sendMode;

private:
    struct RxSeqRecord {
        std::array<uint8_t, 6> mac;
        uint8_t lastSeqId;
    };
    OnAbandon onAbandon;
    uint8_t lastSentSeqId = 0;
    // Last nonzero seqId delivered per sender. Bounded by the physical peer
    // count present in a session.
    std::vector<RxSeqRecord> rxSeq;
};

template <class P>
class ReliableChannel : public ReliableChannelBase {
public:
    using OnReceive = std::function<void(const uint8_t* fromMac, const P&)>;
    using OnDelivered = std::function<void(uint8_t seqId, const uint8_t* toMac)>;

    /// See ReliableChannelBase; the payload struct P is the wire format.
    ReliableChannel(WirelessManager* wirelessManager,
                    Resender* resender,
                    PktType type,
                    OnAbandon onAbandon,
                    Resender::SendMode sendMode = Resender::SendMode::SUPERSEDE_PER_TARGET)
        : ReliableChannelBase(wirelessManager, resender, type,
                              std::move(onAbandon), sendMode) {}

    /// Reliable send: stamps a fresh nonzero seqId and hands the payload to the
    /// Resender for retry-until-ack. Returns the stamped seqId.
    uint8_t sendReliable(const uint8_t* mac, P p) {
        p.seqId = nextSeqId();
        resender->send(mac, packetType, p.seqId,
                       reinterpret_cast<const uint8_t*>(&p), sizeof(P), sendMode);
        return p.seqId;
    }

    /// Fire-and-forget send: seqId 0, no retry, no ack expected.
    void sendOnce(const uint8_t* mac, P p) {
        sendOnceBytes(mac, reinterpret_cast<const uint8_t*>(&p), sizeof(P));
    }

    /// Decode + dispatch one inbound packet: dedup, then the onReceive
    /// callback. No ack is emitted; the sender's Resender is cleared by its own
    /// radio SEND_SUCCESS, not a round-trip. Dedup still matters because a
    /// sender that missed SEND_SUCCESS retransmits, and the duplicate must be
    /// suppressed here.
    bool deliver(const uint8_t* fromMac, const uint8_t* data, size_t len) {
        if (len != sizeof(P)) {
            // A corrupt/truncated ESP-NOW frame (no CRC on this path); log so
            // it's diagnosable rather than silently dropped.
            logLengthMismatch(packetType, len, sizeof(P));
            return false;
        }
        P p;
        std::memcpy(&p, data, sizeof(P));
        if (isDuplicateReliableRx(fromMac, p.seqId)) return true;
        if (onReceiveCallback) onReceiveCallback(fromMac, p);
        return true;
    }

    /// Untyped entry point used by the transport's rx routing.
    bool deliverBytes(const uint8_t* fromMac, const uint8_t* data, size_t len) override {
        return deliver(fromMac, data, len);
    }

    /// Radio send-result for one of this channel's outbound packets, routed
    /// from the driver's send callback via the transport. `data` is the exact
    /// payload handed to the radio, so the stamped seqId reads straight back
    /// out. SEND_SUCCESS clears the pending entry (the peer's MAC acked it);
    /// SEND_FAIL is ignored and left to the backoff timer.
    void onSendResult(const uint8_t* toMac, const uint8_t* data, size_t len,
                      bool success) override {
        if (!success || len != sizeof(P)) return;
        P p;
        std::memcpy(&p, data, sizeof(P));
        if (p.seqId == 0) return;  // fire-and-forget, no pending entry to clear
        resender->onAck(packetType, p.seqId, toMac);
        // SEND_SUCCESS is this design's delivery signal (no ack round-trip). A
        // late radio duplicate can re-fire it, so handlers must be idempotent.
        if (onDeliveredCallback) onDeliveredCallback(p.seqId, toMac);
    }

    /// This channel's payload struct size, for the transport's claim guard.
    size_t payloadSize() const override { return sizeof(P); }

    /// Registers the decoded-payload handler.
    void onReceive(OnReceive cb) { onReceiveCallback = std::move(cb); }

    /// Registers the SEND_SUCCESS observer (see onSendResult).
    void setOnDelivered(OnDelivered cb) { onDeliveredCallback = std::move(cb); }

private:
    OnReceive onReceiveCallback;
    OnDelivered onDeliveredCallback;
};
