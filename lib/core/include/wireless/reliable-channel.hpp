#pragma once

#include <cstddef>

#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <vector>

#include "device/drivers/peer-comms-types.hpp"
#include "wireless/resender.hpp"

class WirelessManager;
class ReliableTransport;

// A ReliableChannel is a typed, reliable pipe for exactly one PktType: game
// code sends a packed payload struct and receives decoded structs back, and
// the channel supplies everything reliability needs underneath — seqId
// allocation, retry-until-delivered on the device's one ReliableTransport, and
// an abandon callback when a peer stays unreachable past the frame's span. The
// struct itself is the wire format (packed, memcpy'd); both ends run the same
// firmware, so field layout is the protocol. ReliableChannelBase holds the
// untyped mechanics; the ReliableChannel<P> template below binds them to a
// payload type.
//
// Constructing a channel is the whole registration: it claims its PktType on
// the WirelessManager's transport and installs the driver handlers, and its
// destructor drops every pending send and both handlers. A channel therefore
// lives exactly as long as the object that owns it, by value or by pointer.
//
// A unicast is cleared by the radio's own SEND_SUCCESS — the peer's MAC acked
// it — so no reply packet exists and deliver() emits nothing.
enum class ChannelMode : uint8_t {
    // Unicast carrying current state: a newer send to a peer obsoletes the
    // prior unacked one, so an older retransmit can never reinstate stale state.
    SUPERSEDE_PER_TARGET,
    // Unicast carrying one item of a stream: every send keeps its own retry
    // slot, so a dropped earlier item still retransmits.
    KEEP_DISTINCT,
};

class ReliableChannelBase {
public:
    using OnAbandon = std::function<void(uint8_t seqId, const uint8_t* targetMac)>;
    using OnDelivered = std::function<void(uint8_t seqId, const uint8_t* toMac)>;

    /**
     * Claims `type` on wirelessManager's transport and installs the driver
     * receive and send-status handlers. With a null wirelessManager (probe-style
     * unit tests), or when `type` is already claimed on this device (a wiring
     * bug, logged), the channel runs without a radio: nothing is sent or received.
     */
    ReliableChannelBase(WirelessManager* wirelessManager, PktType type, ChannelMode mode);
    /**
     * Drops every pending send on this PktType, detaches from the transport,
     * and clears both driver handlers.
     */
    virtual ~ReliableChannelBase();

    /** The PktType this channel claims. */
    PktType type() const { return packetType; }

    /** True when at least one send to this MAC is still awaiting its ack. */
    bool isPending(const uint8_t* mac) const;

    /** Silently drops every pending send to this MAC (no abandon callback). */
    void cancel(const uint8_t* mac);

    /** Silently drops every pending send on this channel, to every recipient. */
    void cancelAll();

    /**
     * Observes delivery to one peer: SEND_SUCCESS on the unicast. A late radio
     * duplicate can re-fire it, so the observer must be idempotent.
     */
    void setOnDelivered(OnDelivered cb) { onDeliveredCallback = std::move(cb); }

    /**
     * Registers the per-recipient give-up handler. Abandonment is a game-level
     * signal, not a log line; cancel() drops pending sends WITHOUT it.
     */
    void setOnAbandon(OnAbandon cb) { onAbandonCallback = std::move(cb); }

    /** The transport gave up on `targetMac` for the frame sent under `seqId`. */
    void onResenderAbandon(uint8_t seqId, const uint8_t* targetMac);

    /**
     * Typed subclass deserializes bytes into its payload type and dispatches
     * to its onReceive callback. Returns true if delivered.
     */
    virtual bool deliverBytes(const uint8_t* fromMac, const uint8_t* data, size_t len) = 0;

    /**
     * Radio send-result for one of this channel's outbound packets. The typed
     * subclass reads the stamped seqId back from `data` and, on success,
     * clears the pending entry.
     */
    virtual void onSendResult(const uint8_t* toMac, const uint8_t* data,
                              size_t len, bool success) = 0;

protected:
    uint8_t nextSeqId();
    // Defined in the .cpp so the logger stays out of this template header.
    static void logLengthMismatch(PktType type, size_t got, size_t want);
    // seqId == 0 is the unsequenced sentinel (see nextSeqId) and is never deduped here.
    bool isDuplicateReliableRx(const uint8_t* fromMac, uint8_t seqId);

    // Both null on a channel that has no radio (see the constructor); every
    // send then no-ops.
    Resender* resender;
    PktType packetType;
    WirelessManager* wirelessManager;
    ChannelMode channelMode;
    OnDelivered onDeliveredCallback;
    OnAbandon onAbandonCallback;

private:
    struct RxSeqRecord {
        std::array<uint8_t, 6> mac;
        uint8_t lastSeqId;
    };
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

    /** See ReliableChannelBase; the payload struct P is the wire format. */
    ReliableChannel(WirelessManager* wirelessManager, PktType type,
                    ChannelMode mode = ChannelMode::SUPERSEDE_PER_TARGET)
        : ReliableChannelBase(wirelessManager, type, mode) {}

    /**
     * Reliable unicast: allocates a fresh nonzero seqId, writes it into `p`,
     * arms retry-until-ack, and returns the id. By reference rather than by
     * value so a 770-byte HeadTransferPayload is not copied onto this frame on
     * the way past; the id lands in the caller's struct as a consequence, which
     * costs nothing because every caller passes a local it built for this send.
     */
    uint8_t sendReliable(const uint8_t* mac, P& p) {
        p.seqId = nextSeqId();
        if (resender != nullptr) {
            resender->send(mac, packetType, p.seqId, reinterpret_cast<const uint8_t*>(&p),
                           sizeof(P), sendMode());
        }
        return p.seqId;
    }

    /**
     * Decode + dispatch one inbound packet, deduping on the sender's last
     * seqId first: a sender that missed SEND_SUCCESS retransmits.
     */
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

    /** Untyped entry for the driver packet handler and the transport's deliverIncoming seam. */
    bool deliverBytes(const uint8_t* fromMac, const uint8_t* data, size_t len) override {
        return deliver(fromMac, data, len);
    }

    /**
     * Radio send-result for one of this channel's outbound packets. `data` is
     * the exact payload handed to the radio, so the stamped seqId reads straight
     * back out. SEND_SUCCESS clears the pending entry (the peer's MAC acked it);
     * SEND_FAIL is left to the backoff timer.
     */
    void onSendResult(const uint8_t* toMac, const uint8_t* data, size_t len,
                      bool success) override {
        if (!success || len != sizeof(P)) return;
        P p;
        std::memcpy(&p, data, sizeof(P));
        if (p.seqId == 0) return;  // fire-and-forget, no pending entry to clear
        if (resender != nullptr) resender->onAck(packetType, p.seqId, toMac);
        // SEND_SUCCESS is the delivery signal whether or not a slot was still
        // armed: a late radio duplicate re-fires it, so observers are idempotent.
        if (onDeliveredCallback) onDeliveredCallback(p.seqId, toMac);
    }

    /** Registers the decoded-payload handler. */
    void onReceive(OnReceive cb) { onReceiveCallback = std::move(cb); }

private:
    Resender::SendMode sendMode() const {
        return channelMode == ChannelMode::KEEP_DISTINCT ? Resender::SendMode::KEEP_DISTINCT
                                                         : Resender::SendMode::SUPERSEDE_PER_TARGET;
    }

    OnReceive onReceiveCallback;
};
