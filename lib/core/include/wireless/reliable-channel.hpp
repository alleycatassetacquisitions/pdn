#pragma once

#include <cstddef>

#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <vector>

#include "device/drivers/peer-comms-types.hpp"
#include "wireless/resender.hpp"

// A typed, reliable pipe for exactly one PktType. The packed payload struct P is
// the wire format. A channel sends no ack packet: the radio's SEND_SUCCESS clears
// a send, and rx dedup covers a sender that missed it and retransmits.
class ReliableChannelBase {
public:
    using OnAbandon = std::function<void(uint8_t seqId, const uint8_t* targetMac)>;

    /**
     * wirelessManager may be nullptr in probe-style unit tests; every use is
     * null-tolerant.
     */
    ReliableChannelBase(WirelessManager* wirelessManager,
                        Resender* resender,
                        PktType type,
                        OnAbandon onAbandon,
                        Resender::SendMode sendMode = Resender::SendMode::SUPERSEDE_PER_TARGET);
    /**
     * Virtual: channels are owned and deleted through the base pointer. Drops
     * both driver registrations the constructor installed.
     */
    virtual ~ReliableChannelBase();

    /** The PktType this channel claims. */
    PktType type() const { return packetType; }

    /** Relays a Resender abandonment to this channel's OnAbandon callback. */
    void onResenderAbandon(uint8_t seqId, const uint8_t* targetMac);

    /** True when at least one send to this MAC is still awaiting its ack. */
    bool isPending(const uint8_t* mac) const;

    /** Silently drops every pending send to this MAC (no abandon callback). */
    void cancel(const uint8_t* mac);

    /**
     * Typed subclass deserializes bytes into its payload type and dispatches
     * to its onReceive callback. Returns false only on a length mismatch.
     */
    virtual bool deliverBytes(const uint8_t* fromMac, const uint8_t* data, size_t len) = 0;

    /**
     * Radio send-result for one of this channel's outbound packets. The typed
     * subclass reads the stamped seqId back from `data` and, on success,
     * clears the pending entry.
     */
    virtual void onSendResult(const uint8_t* toMac, const uint8_t* data,
                              size_t len, bool success) = 0;

    /**
     * sizeof the payload struct this channel is typed on. The transport uses
     * it to tell a same-size re-claim from a different-size collision on the
     * same PktType.
     */
    virtual size_t payloadSize() const = 0;

    /**
     * Rebinds the abandon callback; used when a re-created owner re-claims an
     * already-registered channel so the callback points at the live instance.
     */
    void setOnAbandon(OnAbandon cb) { onAbandon = std::move(cb); }

protected:
    uint8_t nextSeqId();
    WirelessManager* getWirelessManager() const { return wirelessManager; }
    // Defined in the .cpp so the logger stays out of this template header.
    static void logLengthMismatch(PktType type, size_t got, size_t want);
    // seqId == 0 is the unsequenced sentinel (see nextSeqId) and is never deduped here.
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
    // Seeded at construction so a rebooted peer does not systematically restart
    // on the same seqId a receiver's cursor is already holding.
    uint8_t lastSentSeqId;
    // Last nonzero seqId delivered per sender. Receiving costs no peer-table
    // slot, so nothing bounds this but MAX_RX_SENDERS.
    std::vector<RxSeqRecord> rxSeq;
};

template <class P>
class ReliableChannel : public ReliableChannelBase {
public:
    using OnReceive = std::function<void(const uint8_t* fromMac, const P&)>;
    using OnDelivered = std::function<void(uint8_t seqId, const uint8_t* toMac)>;

    /** See ReliableChannelBase; the payload struct P is the wire format. */
    ReliableChannel(WirelessManager* wirelessManager,
                    Resender* resender,
                    PktType type,
                    OnAbandon onAbandon,
                    Resender::SendMode sendMode = Resender::SendMode::SUPERSEDE_PER_TARGET)
        : ReliableChannelBase(wirelessManager, resender, type,
                              std::move(onAbandon), sendMode) {}

    /**
     * Stamps a fresh nonzero seqId into `p`, sends it via the Resender, and returns
     * the id. By reference so a 770-byte HeadTransferPayload is not copied.
     */
    uint8_t sendReliable(const uint8_t* mac, P& p) {
        p.seqId = nextSeqId();
        resender->send(mac, packetType, p.seqId,
                       reinterpret_cast<const uint8_t*>(&p), sizeof(P), sendMode);
        return p.seqId;
    }

    /** Decode + dispatch one inbound packet: dedup, then onReceive. */
    bool deliver(const uint8_t* fromMac, const uint8_t* data, size_t len) {
        if (len != sizeof(P)) {
            // The radio's FCS already rejects corrupt frames, so a wrong length
            // means the sender's payload struct differs (firmware mismatch).
            logLengthMismatch(packetType, len, sizeof(P));
            return false;
        }
        P p;
        std::memcpy(&p, data, sizeof(P));
        if (isDuplicateReliableRx(fromMac, p.seqId)) return true;
        if (onReceiveCallback) onReceiveCallback(fromMac, p);
        return true;
    }

    /** Untyped entry point for the driver packet handler. */
    bool deliverBytes(const uint8_t* fromMac, const uint8_t* data, size_t len) override {
        return deliver(fromMac, data, len);
    }

    /**
     * Radio send-result for one of this channel's outbound packets, routed
     * from the driver's send callback. `data` is the exact payload handed to
     * the radio, so the stamped seqId reads straight back out. SEND_SUCCESS
     * clears the pending entry (the peer's MAC acked it); SEND_FAIL is ignored
     * and left to the backoff timer.
     */
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

    /** This channel's payload struct size, for the transport's claim guard. */
    size_t payloadSize() const override { return sizeof(P); }

    /** Registers the decoded-payload handler. */
    void onReceive(OnReceive cb) { onReceiveCallback = std::move(cb); }

    /** Registers the SEND_SUCCESS observer (see onSendResult). */
    void setOnDelivered(OnDelivered cb) { onDeliveredCallback = std::move(cb); }

    /** Silently drops every pending send whose payload `matches` (no abandon callback). */
    void cancelMatching(const std::function<bool(const P&)>& matches) {
        resender->cancelMatching(packetType, [&matches](const uint8_t* payload, size_t len) {
            if (len != sizeof(P)) return false;
            P typed{};
            memcpy(&typed, payload, sizeof(P));
            return matches(typed);
        });
    }

private:
    OnReceive onReceiveCallback;
    OnDelivered onDeliveredCallback;
};
