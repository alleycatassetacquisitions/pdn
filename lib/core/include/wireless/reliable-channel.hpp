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
// Delivery evidence depends on how a frame is addressed. A unicast is cleared by
// the radio's own SEND_SUCCESS — the peer's MAC acked it — so no reply packet
// exists and deliver() emits nothing. A fan-out goes out as one broadcast frame
// owed to several recipients, and a broadcast's SEND_SUCCESS names none of them,
// so each recipient answers with a kReliableAck that the receiving handler
// sends through ack() once it has admitted the frame.
enum class ChannelMode : uint8_t {
    // Unicast carrying current state: a newer send to a peer obsoletes the
    // prior unacked one, so an older retransmit can never reinstate stale state.
    SUPERSEDE_PER_TARGET,
    // Unicast carrying one item of a stream: every send keeps its own retry
    // slot, so a dropped earlier item still retransmits.
    KEEP_DISTINCT,
    // Broadcast fan-out acked per recipient by packet. Nothing is deduped on
    // receipt: distinct frames interleave on one PktType, and a duplicate still
    // owes its ack or the sender retries it to abandonment.
    FAN_OUT,
};

class ReliableChannelBase {
public:
    using OnDelivered = std::function<void(uint8_t seqId, const uint8_t* toMac)>;

    /**
     * Claims `type` on wirelessManager's transport and installs the driver
     * receive and send-status handlers. wirelessManager may be nullptr in
     * probe-style unit tests; every use is then null-tolerant and nothing is sent.
     */
    ReliableChannelBase(WirelessManager* wirelessManager, PktType type, ChannelMode mode);
    /**
     * Virtual: channels are owned and deleted through the base pointer. Drops
     * every pending send on this PktType and both driver registrations.
     */
    virtual ~ReliableChannelBase();

    /** The PktType this channel claims. */
    PktType type() const { return packetType; }

    /** How this channel's frames are addressed and acked. */
    ChannelMode mode() const { return channelMode; }

    /** True when at least one send to this MAC is still awaiting its ack. */
    bool isPending(const uint8_t* mac) const;

    /**
     * Recipients of the frame sent under `seqId` that still owe an ack. Zero
     * once every one of them has answered or been given up on.
     */
    size_t pendingCount(uint8_t seqId) const;

    /** Silently drops every pending send to this MAC (no abandon callback). */
    void cancel(const uint8_t* mac);

    /** Silently drops every pending send on this channel, to every recipient. */
    void cancelAll();

    /**
     * cancelAll, sparing the frame sent under `keepSeqId`: the frame reporting
     * that a conversation is over owes delivery to exactly the peers that have
     * not heard it yet, so it must outlive the teardown it reports. 0 spares
     * nothing.
     */
    void cancelAllExcept(uint8_t keepSeqId);

    /**
     * Answers a FAN_OUT frame received from `toMac`: sends the kReliableAck
     * that clears this recipient's slot on the sender. The receiving handler
     * calls it only for a frame it admitted, because an ack is a unicast and a
     * unicast to a stranger spends one of the radio's 20 peer-table slots.
     */
    void ack(const uint8_t* toMac, uint8_t seqId);

    /**
     * Observes delivery to one peer: SEND_SUCCESS on a unicast, the peer's
     * kReliableAck on a fan-out. A late radio duplicate can re-fire it, so the
     * observer must be idempotent.
     */
    void setOnDelivered(OnDelivered cb) { onDeliveredCallback = std::move(cb); }

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

    /**
     * The transport gave up on `targetMac` for the frame sent under `seqId`;
     * `payload` is that frame, so the typed subclass can hand the decoded
     * struct to its abandon callback.
     */
    virtual void onAbandon(uint8_t seqId, const uint8_t* targetMac, const uint8_t* payload,
                           size_t len) = 0;

    /** A recipient's kReliableAck for the frame sent under `seqId`. */
    void onPacketAck(const uint8_t* fromMac, uint8_t seqId);

protected:
    uint8_t nextSeqId();
    // Sends one frame straight to the radio with no retry slot: seqId 0 on the
    // wire, nothing owed back.
    void sendUnsequencedBytes(const uint8_t* mac, const uint8_t* data, size_t len);
    // Defined in the .cpp so the logger stays out of this template header.
    static void logLengthMismatch(PktType type, size_t got, size_t want);
    // seqId == 0 is the unsequenced sentinel (see nextSeqId) and is never deduped here.
    bool isDuplicateReliableRx(const uint8_t* fromMac, uint8_t seqId);
    void fireDelivered(uint8_t seqId, const uint8_t* mac);

    // Null when constructed without a WirelessManager; every send then no-ops.
    Resender* resender;
    PktType packetType;
    WirelessManager* wirelessManager;
    ChannelMode channelMode;

private:
    struct RxSeqRecord {
        std::array<uint8_t, 6> mac;
        uint8_t lastSeqId;
    };
    ReliableTransport* transport;
    OnDelivered onDeliveredCallback;
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
    using OnAbandon = std::function<void(uint8_t seqId, const uint8_t* targetMac, const P&)>;

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
     * Reliable fan-out: ONE broadcast frame, with every named recipient
     * expected to answer for it through ack(). Each is cleared independently,
     * but the retry schedule belongs to the frame. Naming no recipients sends
     * nothing. See Resender::sendBroadcast for why a broadcast rather than a
     * unicast per recipient.
     */
    uint8_t sendBroadcast(const std::vector<std::array<uint8_t, 6>>& recipients, P& p) {
        p.seqId = nextSeqId();
        if (resender != nullptr) {
            resender->sendBroadcast(recipients, packetType, p.seqId,
                                    reinterpret_cast<const uint8_t*>(&p), sizeof(P));
        }
        return p.seqId;
    }

    /**
     * Fire-and-forget: seqId 0 on the wire, no retry slot, no ack owed. For
     * frames a receiver must not act on late, where a retransmit would be worse
     * than a loss.
     */
    void sendUnsequenced(const uint8_t* mac, P& p) {
        p.seqId = 0;
        sendUnsequencedBytes(mac, reinterpret_cast<const uint8_t*>(&p), sizeof(P));
    }

    /**
     * Decode + dispatch one inbound packet. A unicast channel dedups on the
     * sender's last seqId first, because a sender that missed SEND_SUCCESS
     * retransmits; a FAN_OUT channel dispatches everything and leaves
     * idempotence to the handler, which has to see the duplicate to ack it.
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
        if (channelMode != ChannelMode::FAN_OUT && isDuplicateReliableRx(fromMac, p.seqId)) {
            return true;
        }
        if (onReceiveCallback) onReceiveCallback(fromMac, p);
        return true;
    }

    /** Untyped entry point used by the transport's rx routing. */
    bool deliverBytes(const uint8_t* fromMac, const uint8_t* data, size_t len) override {
        return deliver(fromMac, data, len);
    }

    /**
     * Radio send-result for one of this channel's outbound packets. `data` is
     * the exact payload handed to the radio, so the stamped seqId reads straight
     * back out. On a unicast, SEND_SUCCESS clears the pending entry (the peer's
     * MAC acked it) and SEND_FAIL is left to the backoff timer. A fan-out's
     * result names the broadcast address and so clears nothing: its evidence
     * arrives as kReliableAck packets.
     */
    void onSendResult(const uint8_t* toMac, const uint8_t* data, size_t len,
                      bool success) override {
        if (channelMode == ChannelMode::FAN_OUT) return;
        if (!success || len != sizeof(P)) return;
        P p;
        std::memcpy(&p, data, sizeof(P));
        if (p.seqId == 0) return;  // fire-and-forget, no pending entry to clear
        if (resender != nullptr) resender->onAck(packetType, p.seqId, toMac);
        fireDelivered(p.seqId, toMac);
    }

    /** Decodes the abandoned frame and hands it to the abandon callback. */
    void onAbandon(uint8_t seqId, const uint8_t* targetMac, const uint8_t* payload,
                   size_t len) override {
        if (!onAbandonCallback || len != sizeof(P)) return;
        P p;
        std::memcpy(&p, payload, sizeof(P));
        onAbandonCallback(seqId, targetMac, p);
    }

    /** Registers the decoded-payload handler. */
    void onReceive(OnReceive cb) { onReceiveCallback = std::move(cb); }

    /**
     * Registers the per-recipient give-up handler. Abandonment is a game-level
     * signal (void a match, abort a tournament), not a log line; cancel() drops
     * pending sends WITHOUT it.
     */
    void setOnAbandon(OnAbandon cb) { onAbandonCallback = std::move(cb); }

private:
    Resender::SendMode sendMode() const {
        return channelMode == ChannelMode::KEEP_DISTINCT ? Resender::SendMode::KEEP_DISTINCT
                                                         : Resender::SendMode::SUPERSEDE_PER_TARGET;
    }

    OnReceive onReceiveCallback;
    OnAbandon onAbandonCallback;
};
